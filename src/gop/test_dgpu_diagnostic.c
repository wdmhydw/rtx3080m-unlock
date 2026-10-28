/* Real capture/orchestration with firmware metadata fixtures. All GPU-writing
   services and GOP methods are NULL, so invoking any would fail the test. */
#include "dgpu_diagnostic.c"
#include <assert.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <stdarg.h>

static EFI_BOOT_SERVICES Bs;
static EFI_SYSTEM_TABLE Table;
static GC_CONTEXT Context;
static EFI_GRAPHICS_OUTPUT_PROTOCOL Gop;
static EFI_GRAPHICS_OUTPUT_PROTOCOL_MODE Mode;
static EFI_GRAPHICS_OUTPUT_MODE_INFORMATION Info;
static EFI_DEVICE_PATH_PROTOCOL Path;
static EFI_FILE_PROTOCOL Report;
static unsigned Count, Allocations, LogCalls, FailLog, Trees, Prepares, Starts, Cancels;
static UINTN TargetPaths, OtherPaths, Unresolved;
static BOOLEAN BadHandle, NoGop, NoMode, NoInfo, NoPath, OtherGpu, ResolveFail, FreeFail;
static EFI_STATUS HandlesStatus, TreeStatus, PrepareStatus, StartStatus, CancelStatus;
static BOOLEAN Log(const char *fmt, ...) {
  LogCalls++;
  if (FailLog && LogCalls >= FailLog) return FALSE;
  if (!strncmp(fmt, "DISPLAY_END ", 12)) {
    va_list args;
    va_start(args, fmt);
    (void)va_arg(args, unsigned long long);
    TargetPaths = va_arg(args, size_t);
    OtherPaths = va_arg(args, size_t);
    Unresolved = va_arg(args, size_t);
    va_end(args);
  }
  return TRUE;
}
static EFI_STATUS EFIAPI Handles(EFI_LOCATE_SEARCH_TYPE t, EFI_GUID *g, VOID *key, UINTN *n,
                                 EFI_HANDLE **out) {
  assert(t == ByProtocol && !memcmp(g, &GopGuid, sizeof(*g)) && !key);
  *n = Count;
  *out = malloc(sizeof(**out) * (Count ? Count : 1));
  assert(*out);
  Allocations++;
  for (UINTN i = 0; i < Count; i++)
    (*out)[i] = (EFI_HANDLE)(i + 1);
  return HandlesStatus;
}
static EFI_STATUS EFIAPI Free(VOID *p) {
  assert(p && Allocations);
  Allocations--;
  free(p);
  return FreeFail ? EFI_DEVICE_ERROR : EFI_SUCCESS;
}
static EFI_STATUS EFIAPI Handle(EFI_HANDLE h, EFI_GUID *g, VOID **out) {
  assert((UINTN)h >= 1 && (UINTN)h <= Count);
  *out = NULL;
  if (!memcmp(g, &GopGuid, sizeof(*g))) {
    if (BadHandle) return EFI_DEVICE_ERROR;
    Gop.Mode = NoMode ? NULL : &Mode;
    Mode.Info = NoInfo ? NULL : &Info;
    *out = NoGop ? NULL : &Gop;
    return EFI_SUCCESS;
  }
  assert(!memcmp(g, &PathGuid, sizeof(*g)));
  if (h == Table.ConsoleOutHandle || NoPath) return EFI_NOT_FOUND;
  *out = &Path;
  return EFI_SUCCESS;
}
static EFI_STATUS EFIAPI Locate(EFI_GUID *g, EFI_DEVICE_PATH_PROTOCOL **p, EFI_HANDLE *out) {
  assert(!memcmp(g, &PciGuid, sizeof(*g)) && *p == &Path);
  *out = ResolveFail ? NULL : OtherGpu ? (EFI_HANDLE)(UINTN)101 : Context.target;
  return ResolveFail ? EFI_NOT_FOUND : EFI_SUCCESS;
}
EFI_STATUS GcReadTree(GC_CONTEXT *c) {
  assert(c == &Context);
  Trees++;
  return TreeStatus;
}
EFI_STATUS GwPrepare(GC_CONTEXT *c, GW_PLAN *p) {
  assert(c == &Context && !p->image);
  Prepares++;
  if (!EFI_ERROR(PrepareStatus)) p->image = (EFI_HANDLE)(UINTN)99;
  return PrepareStatus;
}
EFI_STATUS GwStart(GC_CONTEXT *c, GW_PLAN *p, EFI_FILE_PROTOCOL **report, BOOLEAN verified) {
  assert(c == &Context && p->image == (EFI_HANDLE)(UINTN)99 && verified && *report == &Report);
  Starts++;
  *report = NULL;
  return StartStatus;
}
EFI_STATUS GwCancel(GC_CONTEXT *c, GW_PLAN *p) {
  assert(c == &Context);
  Cancels++;
  if (!EFI_ERROR(CancelStatus)) p->image = NULL;
  return CancelStatus;
}
static VOID Reset(VOID) {
  assert(!Allocations);
  memset(&Bs, 0, sizeof(Bs));
  memset(&Table, 0, sizeof(Table));
  memset(&Context, 0, sizeof(Context));
  memset(&Gop, 0, sizeof(Gop));
  memset(&Mode, 0, sizeof(Mode));
  memset(&Info, 0, sizeof(Info));
  Count = 2;
  LogCalls = FailLog = Trees = Prepares = Starts = Cancels = 0;
  TargetPaths = OtherPaths = Unresolved = 0;
  BadHandle = NoGop = NoMode = NoInfo = NoPath = OtherGpu = ResolveFail = FreeFail = FALSE;
  HandlesStatus = TreeStatus = PrepareStatus = CancelStatus = EFI_SUCCESS;
  StartStatus = EFI_ABORTED;
  Bs.LocateHandleBuffer = Handles;
  Bs.HandleProtocol = Handle;
  Bs.FreePool = Free;
  Bs.LocateDevicePath = Locate;
  Table.BootServices = &Bs;
  Table.ConsoleOutHandle = (EFI_HANDLE)(UINTN)2;
  Mode.MaxMode = 9;
  Mode.SizeOfInfo = sizeof(Info);
  Mode.FrameBufferBase = 0x100000000ULL;
  Mode.FrameBufferSize = 16384000;
  Info.HorizontalResolution = Info.PixelsPerScanLine = 2560;
  Info.VerticalResolution = 1600;
  Info.PixelFormat = PixelBlueGreenRedReserved8BitPerColor;
  Context.table = &Table;
  Context.target = (EFI_HANDLE)(UINTN)100;
  Context.identity = 0x24dc10de;
  Context.subsystem = 0x3f8417aa;
  Context.log = Log;
}
int main(void) {
  Reset();
  assert(GdReadDisplays(&Context) == EFI_SUCCESS && TargetPaths == 1 && !OtherPaths &&
         Unresolved == 1);
  Reset();
  OtherGpu = TRUE;
  assert(GdReadDisplays(&Context) == EFI_SUCCESS && OtherPaths == 1 && !TargetPaths);
  Reset();
  NoPath = TRUE;
  assert(GdReadDisplays(&Context) == EFI_SUCCESS && Unresolved == 2);
  Reset();
  ResolveFail = TRUE;
  assert(GdReadDisplays(&Context) == EFI_SUCCESS && Unresolved == 2);
  Reset();
  HandlesStatus = EFI_DEVICE_ERROR;
  assert(GdReadDisplays(&Context) == EFI_DEVICE_ERROR && !Allocations);
  Reset();
  Count = 65;
  assert(GdReadDisplays(&Context) == EFI_COMPROMISED_DATA && !Allocations);
  Reset();
  Count = 0;
  assert(GdReadDisplays(&Context) == EFI_COMPROMISED_DATA && !Allocations);
  Reset();
  BadHandle = TRUE;
  assert(GdReadDisplays(&Context) == EFI_DEVICE_ERROR);
  Reset();
  NoGop = TRUE;
  assert(GdReadDisplays(&Context) == EFI_COMPROMISED_DATA);
  Reset();
  NoMode = TRUE;
  assert(GdReadDisplays(&Context) == EFI_COMPROMISED_DATA);
  Reset();
  NoInfo = TRUE;
  assert(GdReadDisplays(&Context) == EFI_COMPROMISED_DATA);
  Reset();
  Mode.SizeOfInfo = sizeof(Info) - 1;
  assert(GdReadDisplays(&Context) == EFI_COMPROMISED_DATA);
  Reset();
  FreeFail = TRUE;
  assert(GdReadDisplays(&Context) == EFI_DEVICE_ERROR && !Allocations);
  for (unsigned i = 1; i <= 8; i++) {
    Reset();
    FailLog = i;
    assert(GdReadDisplays(&Context) == EFI_DEVICE_ERROR && !Allocations);
  }
  Reset();
  EFI_FILE_PROTOCOL *report = &Report;
  assert(GdCaptureAndBoot(&Context, &report) == EFI_ABORTED && !report && Trees == 1 &&
         Prepares == 1 && Starts == 1 && Cancels == 1);
  Reset();
  report = &Report;
  Context.identity++;
  assert(GdCaptureAndBoot(&Context, &report) == EFI_UNSUPPORTED && !Trees && !Starts);
  Reset();
  Context.subsystem++;
  assert(GdCaptureAndBoot(&Context, &report) == EFI_UNSUPPORTED && !Trees && !Starts);
  Reset();
  assert(GdCaptureAndBoot(&Context, NULL) == EFI_INVALID_PARAMETER && !Trees);
  Reset();
  report = NULL;
  assert(GdCaptureAndBoot(&Context, &report) == EFI_INVALID_PARAMETER && !Trees);
  Reset();
  report = &Report;
  Context.quiet = TRUE;
  assert(GdCaptureAndBoot(&Context, &report) == EFI_INVALID_PARAMETER && !Trees);
  Reset();
  NoMode = TRUE;
  assert(GdCaptureAndBoot(&Context, &report) == EFI_COMPROMISED_DATA && Trees == 1 && !Starts);
  Reset();
  TreeStatus = EFI_ACCESS_DENIED;
  assert(GdCaptureAndBoot(&Context, &report) == EFI_ACCESS_DENIED && !Prepares && !Starts);
  Reset();
  PrepareStatus = EFI_NOT_FOUND;
  assert(GdCaptureAndBoot(&Context, &report) == EFI_NOT_FOUND && Prepares == 1 && !Starts &&
         Cancels == 1);
  Reset();
  FailLog = 9;
  assert(GdCaptureAndBoot(&Context, &report) == EFI_DEVICE_ERROR && Trees == 1 && !Prepares &&
         !Starts);
  Reset();
  FailLog = 10;
  assert(GdCaptureAndBoot(&Context, &report) == EFI_DEVICE_ERROR && Prepares == 1 && !Starts &&
         Cancels == 1);
  Reset();
  StartStatus = EFI_LOAD_ERROR;
  assert(GdCaptureAndBoot(&Context, &report) == EFI_LOAD_ERROR && Starts == 1 && Cancels == 1);
  Reset();
  report = &Report;
  CancelStatus = EFI_DEVICE_ERROR;
  assert(GdCaptureAndBoot(&Context, &report) == EFI_DEVICE_ERROR && Starts == 1 && Cancels == 1);
  assert(!Allocations);
  puts("PASS 34 display/diagnostic scenarios; GPU-writing services unavailable");
  return 0;
}
