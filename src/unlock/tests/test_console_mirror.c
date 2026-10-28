/* Actual DisplayCheck and PCI routing, with host-only EFI service fixtures. */
#ifdef TEST_ORIGINAL
#include "../../../build/console-check/original-app.c"
#else
#include "../../../build/console-check/patched-app.c"
#endif
#include <stdio.h>
#include <stdlib.h>
#include <string.h>

GU_PLATFORM Gu;
GU_RUNTIME_PROFILE GuProfile;
static EFI_BOOT_SERVICES TestBs;
static EFI_SYSTEM_TABLE TestTable;
static EFI_PCI_IO_PROTOCOL TestTarget, TestIntel;
static EFI_GRAPHICS_OUTPUT_PROTOCOL TestGops[4];
static EFI_GRAPHICS_OUTPUT_PROTOCOL_MODE TestModes[4];
static EFI_GRAPHICS_OUTPUT_MODE_INFORMATION TestInfos[4];
static EFI_DEVICE_PATH_PROTOCOL TestPaths[4];
static BOOLEAN HasPath[4], Reverse, TargetRoute, WrongClass, LogOk, QueryFail, FreeFail;
static EFI_STATUS PathStatus[4];
static UINTN TestCount, LiveAllocations;
static unsigned Checks;
#define H(i) ((EFI_HANDLE)(UINTN)(0x10 + (i)))
#define TARGET_HANDLE ((EFI_HANDLE)(UINTN)0x20)
#define INTEL_HANDLE ((EFI_HANDLE)(UINTN)0x21)

VOID GuLog(const char *format, ...) { (void)format; }
BOOLEAN GuLogReady(VOID) { return LogOk; }
VOID GuFree(VOID *p) {
  if (p) {
    free(p);
    LiveAllocations--;
  }
}
static VOID *TestAlloc(UINTN bytes) {
  VOID *p = calloc(1, bytes);
  if (!p) abort();
  LiveAllocations++;
  return p;
}
static EFI_STATUS EFIAPI TestFree(VOID *p) {
  GuFree(p);
  return FreeFail ? EFI_DEVICE_ERROR : EFI_SUCCESS;
}
static EFI_STATUS EFIAPI TestLocate(EFI_LOCATE_SEARCH_TYPE type, EFI_GUID *guid, VOID *key,
                                    UINTN *count, EFI_HANDLE **handles) {
  (void)type;
  (void)key;
  if (memcmp(guid, &GopGuid, sizeof(*guid))) abort();
  *count = TestCount;
  *handles = TestAlloc(sizeof(**handles) * (TestCount ? TestCount : 1));
  for (UINTN i = 0; i < TestCount; i++)
    (*handles)[i] = H(Reverse ? TestCount - 1 - i : i);
  return EFI_SUCCESS;
}
static EFI_STATUS EFIAPI TestHandle(EFI_HANDLE h, EFI_GUID *guid, VOID **out) {
  *out = NULL;
  if (!memcmp(guid, &PciGuid, sizeof(*guid))) {
    if (h == TARGET_HANDLE) *out = &TestTarget;
    if (h == INTEL_HANDLE) *out = &TestIntel;
    return *out ? EFI_SUCCESS : EFI_UNSUPPORTED;
  }
  UINTN i = (UINTN)h - 0x10;
  if (i >= 4) return EFI_UNSUPPORTED;
  if (!memcmp(guid, &GopGuid, sizeof(*guid))) {
    *out = &TestGops[i];
    return EFI_SUCCESS;
  }
  if (!memcmp(guid, &PathGuid, sizeof(*guid))) {
    if (HasPath[i]) *out = &TestPaths[i];
    return PathStatus[i];
  }
  return EFI_UNSUPPORTED;
}
static EFI_STATUS EFIAPI TestResolve(EFI_GUID *guid, EFI_DEVICE_PATH_PROTOCOL **path,
                                     EFI_HANDLE *handle) {
  (void)guid;
  (void)path;
  *handle = TargetRoute ? TARGET_HANDLE : INTEL_HANDLE;
  return EFI_SUCCESS;
}
static EFI_STATUS EFIAPI TestRead(EFI_PCI_IO_PROTOCOL *pci, EFI_PCI_IO_PROTOCOL_WIDTH width,
                                  UINT32 offset, UINTN count, VOID *buffer) {
  if (pci != &TestIntel || width != EfiPciIoWidthUint32 || offset || count != 3) abort();
  UINT32 *v = buffer;
  v[0] = 0x9a608086;
  v[1] = 2;
  v[2] = WrongClass ? 0x02000000 : 0x03000000;
  return EFI_SUCCESS;
}
static EFI_STATUS EFIAPI TestLocation(EFI_PCI_IO_PROTOCOL *pci, UINTN *segment, UINTN *bus,
                                      UINTN *device, UINTN *function) {
  *segment = *function = 0;
  *bus = pci == &TestTarget ? 1 : 0;
  *device = pci == &TestTarget ? 0 : 2;
  return EFI_SUCCESS;
}
static EFI_STATUS EFIAPI TestQuery(EFI_GRAPHICS_OUTPUT_PROTOCOL *gop, UINT32 mode, UINTN *bytes,
                                   EFI_GRAPHICS_OUTPUT_MODE_INFORMATION **info) {
  (void)gop;
  (void)mode;
  *info = NULL;
  if (QueryFail) return EFI_DEVICE_ERROR;
  *bytes = sizeof(**info);
  *info = TestAlloc(*bytes);
  **info = TestInfos[0];
  return EFI_SUCCESS;
}
static VOID Setup(VOID) {
  if (LiveAllocations) abort();
  memset(&Gu, 0, sizeof(Gu));
  memset(&TestBs, 0, sizeof(TestBs));
  memset(&TestTable, 0, sizeof(TestTable));
  memset(&TestTarget, 0, sizeof(TestTarget));
  memset(&TestIntel, 0, sizeof(TestIntel));
  memset(TestGops, 0, sizeof(TestGops));
  memset(TestModes, 0, sizeof(TestModes));
  memset(TestInfos, 0, sizeof(TestInfos));
  memset(HasPath, 0, sizeof(HasPath));
  Reverse = TargetRoute = WrongClass = QueryFail = FreeFail = FALSE;
  LogOk = TRUE;
  TestCount = 2;
  TestBs.LocateHandleBuffer = TestLocate;
  TestBs.HandleProtocol = TestHandle;
  TestBs.LocateDevicePath = TestResolve;
  TestBs.FreePool = TestFree;
  TestIntel.Pci.Read = TestRead;
  TestIntel.GetLocation = TestLocation;
  TestTarget.GetLocation = TestLocation;
  TestTable.BootServices = &TestBs;
  TestTable.ConsoleOutHandle = H(1);
  Table = &TestTable;
  Gu.bs = &TestBs;
  Gu.pci = &TestTarget;
  Gu.handle = TARGET_HANDLE;
  for (UINTN i = 0; i < 4; i++) {
    TestInfos[i].HorizontalResolution = 2560;
    TestInfos[i].VerticalResolution = 1600;
    TestInfos[i].PixelsPerScanLine = 2560;
    TestInfos[i].PixelFormat = PixelBlueGreenRedReserved8BitPerColor;
    TestModes[i].Info = &TestInfos[i];
    TestModes[i].SizeOfInfo = sizeof(TestInfos[i]);
    TestModes[i].Mode = 0;
    TestModes[i].MaxMode = 9;
    TestModes[i].FrameBufferBase = 0x4000000000ULL;
    TestModes[i].FrameBufferSize = 16384000;
    TestGops[i].Mode = &TestModes[i];
    TestGops[i].QueryMode = TestQuery;
    PathStatus[i] = EFI_UNSUPPORTED;
  }
  HasPath[0] = TRUE;
  PathStatus[0] = EFI_SUCCESS;
}
static VOID Expect(const char *name, BOOLEAN expected) {
  BOOLEAN result = DisplayCheck();
  if (result != expected || LiveAllocations) {
    printf("FAIL %s: result=%u expected=%u allocations=%zu\n", name, result, expected,
           (size_t)LiveAllocations);
    exit(1);
  }
  Checks++;
  printf("PASS %s\n", name);
}
int main(int argc, char **argv) {
  (void)argc;
  (void)argv;
#ifdef TEST_ORIGINAL
  Setup();
  Expect("original code rejects captured Lenovo console layout", FALSE);
#else
  Setup();
  Expect("captured Lenovo console layout", TRUE);
  Setup();
  Reverse = TRUE;
  Expect("console enumerated before physical GOP", TRUE);
  Setup();
  PathStatus[1] = EFI_NOT_FOUND;
  Expect("path protocol absent", TRUE);
  Setup();
  TestCount = 1;
  Expect("one physical GOP without console alias", TRUE);
  Setup();
  TestModes[1].FrameBufferBase++;
  Expect("different framebuffer address", FALSE);
  Setup();
  TestModes[1].FrameBufferSize--;
  Expect("different framebuffer size", FALSE);
  Setup();
  TestInfos[1].HorizontalResolution--;
  Expect("different width", FALSE);
  Setup();
  TestInfos[1].VerticalResolution--;
  Expect("different height", FALSE);
  Setup();
  TestInfos[1].PixelsPerScanLine++;
  Expect("different stride", FALSE);
  Setup();
  TestInfos[1].Version++;
  Expect("different version", FALSE);
  Setup();
  TestInfos[1].PixelFormat = PixelRedGreenBlueReserved8BitPerColor;
  Expect("different pixel format", FALSE);
  Setup();
  TestInfos[1].PixelInformation.RedMask = 1;
  Expect("different red mask", FALSE);
  Setup();
  TestInfos[1].PixelInformation.GreenMask = 1;
  Expect("different green mask", FALSE);
  Setup();
  TestInfos[1].PixelInformation.BlueMask = 1;
  Expect("different blue mask", FALSE);
  Setup();
  TestInfos[1].PixelInformation.ReservedMask = 1;
  Expect("different reserved mask", FALSE);
  Setup();
  TestModes[1].Mode++;
  Expect("different mode number", FALSE);
  Setup();
  TestModes[1].MaxMode++;
  Expect("different mode count", FALSE);
  Setup();
  TestModes[1].SizeOfInfo++;
  Expect("different info size", FALSE);
  Setup();
  TestModes[1].MaxMode = 0;
  Expect("invalid mode count", FALSE);
  Setup();
  TestModes[1].Mode = 9;
  Expect("mode outside advertised range", FALSE);
  Setup();
  TestModes[1].Info = NULL;
  Expect("missing console info", FALSE);
  Setup();
  TestModes[1].SizeOfInfo = 35;
  Expect("short console info", FALSE);
  Setup();
  TestGops[1].Mode = NULL;
  Expect("missing console mode", FALSE);
  Setup();
  TestModes[0].Info = NULL;
  Expect("missing physical info", FALSE);
  Setup();
  TestModes[0].SizeOfInfo = 35;
  Expect("short physical info", FALSE);
  Setup();
  TestModes[0].FrameBufferBase = 0;
  Expect("missing physical framebuffer", FALSE);
  Setup();
  TestModes[0].FrameBufferSize = 4;
  Expect("physical framebuffer too small", FALSE);
  Setup();
  TestInfos[0].PixelsPerScanLine = 1;
  Expect("invalid physical stride", FALSE);
  Setup();
  TestInfos[0].PixelFormat = PixelBltOnly;
  Expect("physical GOP has no RGB buffer", FALSE);
  Setup();
  TestTable.ConsoleOutHandle = H(3);
  Expect("pathless interface is not system console", FALSE);
  Setup();
  PathStatus[1] = EFI_DEVICE_ERROR;
  Expect("device path error", FALSE);
  Setup();
  PathStatus[1] = EFI_SUCCESS;
  Expect("success with null device path", FALSE);
  Setup();
  TargetRoute = TRUE;
  Expect("GOP belongs to target NVIDIA", FALSE);
  Setup();
  WrongClass = TRUE;
  Expect("GOP resolves to non-display PCI device", FALSE);
  Setup();
  HasPath[0] = FALSE;
  PathStatus[0] = EFI_UNSUPPORTED;
  Expect("no validated physical display", FALSE);
  Setup();
  TestCount = 3;
  HasPath[2] = TRUE;
  PathStatus[2] = EFI_SUCCESS;
  Expect("multiple physical outputs", FALSE);
  Setup();
  TestCount = 3;
  Expect("extra unknown GOP", FALSE);
  Setup();
  LogOk = FALSE;
  Expect("logging failure", FALSE);
  Setup();
  TestModes[1].FrameBufferBase = 0;
  Expect("console base missing but size nonzero", FALSE);
  Setup();
  TestModes[1].FrameBufferSize = 0;
  Expect("console size missing but base nonzero", FALSE);
  Setup();
  TestModes[1].FrameBufferBase = TestModes[1].FrameBufferSize = 0;
  TestInfos[1].PixelFormat = PixelBltOnly;
  Expect("existing blit-only console", TRUE);
  Setup();
  TestModes[1].FrameBufferBase = TestModes[1].FrameBufferSize = 0;
  TestModes[1].Info = NULL;
  TestModes[1].SizeOfInfo = 0;
  Expect("existing QueryMode forwarding", TRUE);
  QueryFail = TRUE;
  Expect("forwarding QueryMode error", FALSE);
  QueryFail = FALSE;
  FreeFail = TRUE;
  Expect("forwarding buffer release error", FALSE);
  Setup();
  Expect("first valid snapshot", TRUE);
  TestModes[1].FrameBufferBase++;
  Expect("changed console rejected on recheck", FALSE);
#endif
  printf("Display check scenarios passed: %u. No hardware services were called.\n", Checks);
  return 0;
}
