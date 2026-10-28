/* Actual Lenovo state machine. Driver callbacks are executable thunks placed
   at the observed offsets, so production fingerprint checks remain enabled. */
#ifdef LC_LEGACY_REPRO
#include "../../build/lenovo-cycle-check/legacy-cycle.c"
#elif defined(LC_V7_REPRO)
#include "../../build/lenovo-cycle-check/previous-cycle.c"
#elif defined(LC_V8_REPRO)
#include "../../build/lenovo-cycle-check/recovery-cycle.c"
#else
#include "lenovo_cycle.c"
#endif
#include <assert.h>
#include <stdio.h>
#include <stdlib.h>

extern UINT8 TestImage0[], TestImage1[], TestImage2[], TestImage3[];
#define DECL(N)                                                                                    \
  EFI_STATUS EFIAPI TestSupported##N(EFI_DRIVER_BINDING_PROTOCOL *, EFI_HANDLE,                    \
                                     EFI_DEVICE_PATH_PROTOCOL *);                                  \
  EFI_STATUS EFIAPI TestStart##N(EFI_DRIVER_BINDING_PROTOCOL *, EFI_HANDLE,                        \
                                 EFI_DEVICE_PATH_PROTOCOL *);                                      \
  EFI_STATUS EFIAPI TestStop##N(EFI_DRIVER_BINDING_PROTOCOL *, EFI_HANDLE, UINTN, EFI_HANDLE *)
DECL(0);
DECL(1);
DECL(2);
DECL(3);
EFI_STATUS EFIAPI TestGopQuery3(EFI_GRAPHICS_OUTPUT_PROTOCOL *, UINT32, UINTN *,
                                EFI_GRAPHICS_OUTPUT_MODE_INFORMATION **);
EFI_STATUS EFIAPI TestGopSet3(EFI_GRAPHICS_OUTPUT_PROTOCOL *, UINT32);
EFI_STATUS EFIAPI TestGopBlt3(EFI_GRAPHICS_OUTPUT_PROTOCOL *, EFI_GRAPHICS_OUTPUT_BLT_PIXEL *,
                              EFI_GRAPHICS_OUTPUT_BLT_OPERATION, UINTN, UINTN, UINTN, UINTN, UINTN,
                              UINTN, UINTN);
static EFI_BOOT_SERVICES Bs;
static EFI_SYSTEM_TABLE Table;
static GC_CONTEXT Context;
static EFI_PCI_IO_PROTOCOL Pci, WrongPci;
static EFI_DRIVER_BINDING_PROTOCOL Bind[4];
static EFI_LOADED_IMAGE_PROTOCOL Images[4];
static EFI_SIMPLE_TEXT_INPUT_PROTOCOL Input, OtherInput;
static EFI_SIMPLE_TEXT_OUTPUT_PROTOCOL Output, Err;
static EFI_GRAPHICS_OUTPUT_PROTOCOL Gop, Mirror;
static EFI_GRAPHICS_OUTPUT_PROTOCOL_MODE Mode, MirrorMode;
static EFI_GRAPHICS_OUTPUT_MODE_INFORMATION Info, MirrorInfo;
static EFI_DEVICE_PATH_PROTOCOL Path;
static EFI_GUID ExtraGuid = {0x1234, 0, 0, {0}};
static UINT32 Config[64], Initial[64], Mask;
static UINTN ActiveChild;
static unsigned Allocations, Stops, Starts, Claims, Closes, DirectWrites, Tests;
static unsigned FailStop, FailStart, FailSupported;
static UINT32 AutoMask[4], StartRoles, RecoveryScans;
static unsigned DriverChangedAfterStart;
static BOOLEAN Exclusive, InNotify, Recreate, ExtraRoot, ExtraChild, BadCount, MoreGops,
    MirrorMismatch, ResidualVirtual, ResidualPhysical, FailClaim, WrongClaim, FailClose,
    BadRestoreConfig, BadRestoreMode, ExtraAfterStop, MasterAfterStop, ChangedInput, FailIntentLog,
    FailResultLog, RecordsError, ResidualPath, VirtualFramebuffer, IsolationEnumError,
    FailIsolationLog, FailRecoveryLog, OwnerOnRecovery, OwnerOnNext, ChildOwnerAfterStart,
    InputAfterStart, FailStateLog, FailRecordLog, RecordsAfterStart, CountAfterStart;
#ifdef GC_LENOVO_VIRTUAL_RELEASE
static unsigned Withdraws, Publishes, Raises;
static UINT32 InstallNotifyMask, PendingNotifyMask;
static BOOLEAN VirtualHidden, VirtualPublished, FailWithdraw, IgnoreWithdraw, FailPublish,
    ChangePublishHandle, ChangePublishedInterface, CorruptWithdrawnMode, BusyVirtualAfterStop,
    ChangedVirtualAfterStop, ExtraGopAfterStop, RaceBeforeWithdraw, ReappearBeforeClaim,
    ExtraOwnerOnPublish;
static const char *FailVirtualLog;
#endif
static EFI_HANDLE H(UINTN n) { return (EFI_HANDLE)n; }
#define ROOT H(1)
#define CHILD H(ActiveChild)
#define CONSOLE H(3)
#define KEYBOARD H(4)
#define STDERR_HANDLE H(5)

static VOID *Alloc(UINTN size) {
  VOID *p = calloc(1, size ? size : 1);
  assert(p);
  Allocations++;
  return p;
}
static EFI_STATUS EFIAPI Pool(EFI_MEMORY_TYPE t, UINTN n, VOID **p) {
  assert(t == EfiLoaderData);
  *p = Alloc(n);
  return EFI_SUCCESS;
}
static EFI_STATUS EFIAPI FreePool(VOID *p) {
  assert(p && Allocations);
  Allocations--;
  free(p);
  return EFI_SUCCESS;
}
static BOOLEAN Log(const char *fmt, ...) {
  if (FailIntentLog && !strncmp(fmt, "LC_STOP_INTENT", 14)) return FALSE;
  if (FailResultLog && Stops && !strncmp(fmt, "LC_STOP_RESULT", 14)) return FALSE;
  if (FailIsolationLog && !strncmp(fmt, "LC_OLD_INTERFACE", 16)) return FALSE;
  if (!strncmp(fmt, "LC_ISOLATION_DECLINED", 21)) {
    if (FailRecoveryLog) return FALSE;
    if (OwnerOnRecovery) ExtraRoot = TRUE;
  }
  if (!strncmp(fmt, "LC_RECOVERY_SCAN", 16)) RecoveryScans++;
  if (FailStateLog && !strncmp(fmt, "LC_RECOVERY_STATE", 17)) return FALSE;
  if (FailRecordLog && !strncmp(fmt, "LC_RECOVERY_RECORD", 18)) return FALSE;
  if (OwnerOnNext && !strncmp(fmt, "LC_RECOVERY_NEXT", 16)) ExtraRoot = TRUE;
#ifdef GC_LENOVO_VIRTUAL_RELEASE
  if (FailVirtualLog && strstr(fmt, FailVirtualLog)) return FALSE;
#endif
  return TRUE;
}
static EFI_STATUS EFIAPI Read(EFI_PCI_IO_PROTOCOL *p, EFI_PCI_IO_PROTOCOL_WIDTH w, UINT32 off,
                              UINTN n, VOID *out) {
  assert(p == &Pci && w == EfiPciIoWidthUint32 && off == 0 && n == 64);
  memcpy(out, Config, sizeof(Config));
  return EFI_SUCCESS;
}
static EFI_STATUS EFIAPI Write(EFI_PCI_IO_PROTOCOL *p, EFI_PCI_IO_PROTOCOL_WIDTH w, UINT32 off,
                               UINTN n, VOID *out) {
  (void)p;
  (void)w;
  (void)off;
  (void)n;
  (void)out;
  DirectWrites++;
  assert(0);
  return EFI_DEVICE_ERROR;
}
static EFI_STATUS EFIAPI Attributes(EFI_PCI_IO_PROTOCOL *p,
                                    EFI_PCI_IO_PROTOCOL_ATTRIBUTE_OPERATION op, UINT64 attrs,
                                    UINT64 *result) {
  (void)p;
  (void)op;
  (void)attrs;
  (void)result;
  DirectWrites++;
  assert(0);
  return EFI_DEVICE_ERROR;
}
static EFI_STATUS EFIAPI Handle(EFI_HANDLE h, EFI_GUID *g, VOID **out) {
  *out = NULL;
  if (Eq(g, &BindingGuid) && (UINTN)h >= 10 && (UINTN)h < 14) {
    *out = &Bind[(UINTN)h - 10];
    return EFI_SUCCESS;
  }
  if (Eq(g, &LoadedGuid) && (UINTN)h >= 20 && (UINTN)h < 24) {
    *out = &Images[(UINTN)h - 20];
    return EFI_SUCCESS;
  }
  if (h == ROOT && Eq(g, &PciGuid)) *out = &Pci;
  if (h == KEYBOARD && Eq(g, &InputGuid)) *out = Table.ConIn;
  if (h == CONSOLE && Eq(g, &TextGuid)) *out = &Output;
  if (h == STDERR_HANDLE && Eq(g, &TextGuid)) *out = &Err;
  if (h == CHILD && ResidualPath && Eq(g, &PathGuid)) *out = &Path;
  if (h == CHILD && ((Mask & 2) || ResidualPhysical)) {
    if (Eq(g, &PathGuid)) *out = &Path;
    if (Eq(g, &GopGuid)) *out = &Gop;
    if (Eq(g, &TextGuid) && (Mask & 4)) *out = &Output;
  }
  BOOLEAN virtual_present = (Mask & 32) || ResidualVirtual;
#ifdef GC_LENOVO_VIRTUAL_RELEASE
  virtual_present = !VirtualHidden && (virtual_present || VirtualPublished);
#endif
  if (h == CONSOLE && Eq(g, &GopGuid) && virtual_present) {
    if (!(Mask & 32) && !VirtualFramebuffer) {
      MirrorMode.FrameBufferBase = 0;
      MirrorMode.FrameBufferSize = 0;
    }
    *out = &Mirror;
#ifdef GC_LENOVO_VIRTUAL_RELEASE
    if (VirtualPublished && ChangePublishedInterface) *out = &Gop;
#endif
  }
  return *out ? EFI_SUCCESS : EFI_UNSUPPORTED;
}
static EFI_STATUS EFIAPI Locate(EFI_GUID *g, EFI_DEVICE_PATH_PROTOCOL **path, EFI_HANDLE *out) {
  assert(Eq(g, &PciGuid) && *path == &Path);
  *out = ROOT;
  return EFI_SUCCESS;
}
static EFI_STATUS EFIAPI Handles(EFI_LOCATE_SEARCH_TYPE t, EFI_GUID *g, VOID *key, UINTN *n,
                                 EFI_HANDLE **out) {
  assert(t == ByProtocol && Eq(g, &GopGuid) && !key);
  *out = NULL;
  *n = 0;
  if (IsolationEnumError && Stops == 5) return EFI_DEVICE_ERROR;
  EFI_HANDLE list[3];
  if ((Mask & 2) || ResidualPhysical) list[(*n)++] = CHILD;
  BOOLEAN virtual_present = (Mask & 32) || ResidualVirtual;
#ifdef GC_LENOVO_VIRTUAL_RELEASE
  virtual_present = !VirtualHidden && (virtual_present || VirtualPublished);
#endif
  if (virtual_present) list[(*n)++] = CONSOLE;
  if (MoreGops) list[(*n)++] = H(77);
  if (!*n) return EFI_NOT_FOUND;
  *out = Alloc(*n * sizeof(**out));
  memcpy(*out, list, *n * sizeof(**out));
  return EFI_SUCCESS;
}
static EFI_STATUS EFIAPI Protocols(EFI_HANDLE h, EFI_GUID ***out, UINTN *n) {
  *n = 0;
  *out = NULL;
  if (RecordsError) return EFI_DEVICE_ERROR;
  EFI_GUID *list[5];
  if (h == ROOT) {
    list[(*n)++] = &PciGuid;
    list[(*n)++] = &PathGuid;
    list[(*n)++] = &ExtraGuid;
  } else if (h == CHILD && (Mask & 2)) {
    list[(*n)++] = &PathGuid;
    list[(*n)++] = &GopGuid;
    if (Mask & 12) list[(*n)++] = &TextGuid;
    if (Mask & 56) list[(*n)++] = &ConsoleDeviceGuid;
    list[(*n)++] = &ExtraGuid;
  } else if (h == KEYBOARD)
    list[(*n)++] = &InputGuid;
  else if (h == CONSOLE || h == STDERR_HANDLE) {
    list[(*n)++] = &TextGuid;
#ifdef GC_LENOVO_VIRTUAL_RELEASE
    if (h == CONSOLE && !VirtualHidden && ((Mask & 32) || ResidualVirtual || VirtualPublished))
      list[(*n)++] = &GopGuid;
#endif
  } else
    return EFI_NOT_FOUND;
  *out = Alloc(*n * sizeof(**out));
  memcpy(*out, list, *n * sizeof(**out));
  return EFI_SUCCESS;
}
static EFI_STATUS EFIAPI Opens(EFI_HANDLE h, EFI_GUID *g, EFI_OPEN_PROTOCOL_INFORMATION_ENTRY **out,
                               UINTN *n) {
  EFI_OPEN_PROTOCOL_INFORMATION_ENTRY list[5] = {{H(99), h, 1, 7}};
  *n = 1;
  UINT32 count = BadCount ? 2 : 1;
  if (h == ROOT) {
    if (Eq(g, &PciGuid) && (Mask & 1))
      list[(*n)++] = (EFI_OPEN_PROTOCOL_INFORMATION_ENTRY){H(10), ROOT, 16, count};
    if (Eq(g, &PathGuid) && (Mask & 2))
      list[(*n)++] = (EFI_OPEN_PROTOCOL_INFORMATION_ENTRY){H(10), CHILD, 8, count};
    if (Eq(g, &ExtraGuid) && (ExtraRoot || (ExtraAfterStop && Stops)))
      list[(*n)++] = (EFI_OPEN_PROTOCOL_INFORMATION_ENTRY){H(88), ROOT, 16, 1};
    if (Eq(g, &PciGuid) && Exclusive)
      list[(*n)++] = (EFI_OPEN_PROTOCOL_INFORMATION_ENTRY){Context.image, NULL, 32, 1};
  } else if (h == CHILD) {
    if (Eq(g, &GopGuid) && (Mask & 4))
      list[(*n)++] = (EFI_OPEN_PROTOCOL_INFORMATION_ENTRY){H(11), CHILD, 16, count};
    if (Eq(g, &TextGuid) && (Mask & 8))
      list[(*n)++] = (EFI_OPEN_PROTOCOL_INFORMATION_ENTRY){H(12), CHILD, 16, count};
    if (Eq(g, &ConsoleDeviceGuid)) {
      if (Mask & 16) list[(*n)++] = (EFI_OPEN_PROTOCOL_INFORMATION_ENTRY){H(13), CHILD, 16, count};
      if (Mask & 32) list[(*n)++] = (EFI_OPEN_PROTOCOL_INFORMATION_ENTRY){H(13), CONSOLE, 8, count};
    }
    if (Eq(g, &ExtraGuid) && ExtraChild)
      list[(*n)++] = (EFI_OPEN_PROTOCOL_INFORMATION_ENTRY){H(88), CHILD, 16, 1};
  } else if (h == KEYBOARD)
    list[(*n)++] = (EFI_OPEN_PROTOCOL_INFORMATION_ENTRY){H(66), KEYBOARD, 16, 1};
#ifdef GC_LENOVO_VIRTUAL_RELEASE
  else if (h == CONSOLE && Eq(g, &GopGuid) && BusyVirtualAfterStop && Stops == 5)
    list[(*n)++] = (EFI_OPEN_PROTOCOL_INFORMATION_ENTRY){H(88), CONSOLE, 16, 1};
#endif
  *out = Alloc(*n * sizeof(**out));
  memcpy(*out, list, *n * sizeof(**out));
  return EFI_SUCCESS;
}
static EFI_TPL EFIAPI Raise(EFI_TPL t) {
  assert(t == TPL_NOTIFY && !InNotify);
  InNotify = TRUE;
#ifdef GC_LENOVO_VIRTUAL_RELEASE
  Raises++;
  if (RaceBeforeWithdraw && Raises == 1) ExtraRoot = TRUE;
  if (ReappearBeforeClaim && Withdraws && !Publishes) VirtualHidden = FALSE;
#endif
  return TPL_APPLICATION;
}
static VOID EFIAPI Restore(EFI_TPL t) {
  assert(t == TPL_APPLICATION && InNotify);
  InNotify = FALSE;
#ifdef GC_LENOVO_VIRTUAL_RELEASE
  if (PendingNotifyMask) {
    Mask = PendingNotifyMask;
    PendingNotifyMask = 0;
    ActiveChild = 102;
    memcpy(Config, Initial, sizeof(Config));
    MirrorMode = Mode;
    MirrorMode.Info = &MirrorInfo;
  }
#endif
}
#ifdef GC_LENOVO_VIRTUAL_RELEASE
static EFI_STATUS EFIAPI Uninstall(EFI_HANDLE h, EFI_GUID *g, VOID *v) {
  assert(InNotify && !Mask && !Exclusive && h == CONSOLE && Eq(g, &GopGuid) && v == &Mirror);
  assert(!VirtualHidden && !Withdraws);
  Withdraws++;
  if (FailWithdraw) return EFI_ACCESS_DENIED;
  if (!IgnoreWithdraw) VirtualHidden = TRUE;
  if (CorruptWithdrawnMode) MirrorMode.FrameBufferBase++;
  return EFI_SUCCESS;
}
static EFI_STATUS EFIAPI Install(EFI_HANDLE *h, EFI_GUID *g, EFI_INTERFACE_TYPE type, VOID *v) {
  assert(InNotify && !Mask && !Exclusive && *h == CONSOLE && Eq(g, &GopGuid) && v == &Mirror &&
         type == EFI_NATIVE_INTERFACE && VirtualHidden && Withdraws == 1 && !Publishes);
  Publishes++;
  if (FailPublish) return EFI_OUT_OF_RESOURCES;
  VirtualHidden = FALSE;
  VirtualPublished = TRUE;
  if (ChangePublishHandle) *h = H(56);
  if (ExtraOwnerOnPublish) ExtraRoot = TRUE;
  PendingNotifyMask = InstallNotifyMask;
  return EFI_SUCCESS;
}
#endif
static EFI_STATUS EFIAPI Open(EFI_HANDLE h, EFI_GUID *g, VOID **out, EFI_HANDLE agent,
                              EFI_HANDLE controller, UINT32 attr) {
  assert(InNotify && !Mask && h == ROOT && Eq(g, &PciGuid) && agent == Context.image &&
         !controller && attr == 32 && !Exclusive);
  Claims++;
  *out = NULL;
  if (FailClaim) return EFI_ACCESS_DENIED;
  Exclusive = TRUE;
  *out = WrongClaim ? &WrongPci : &Pci;
  return EFI_SUCCESS;
}
static EFI_STATUS EFIAPI Close(EFI_HANDLE h, EFI_GUID *g, EFI_HANDLE agent, EFI_HANDLE controller) {
  assert(!InNotify && Exclusive && h == ROOT && Eq(g, &PciGuid) && agent == Context.image &&
         !controller);
  Closes++;
  if (FailClose) return EFI_DEVICE_ERROR;
  Exclusive = FALSE;
  return EFI_SUCCESS;
}
EFI_STATUS EFIAPI MockSupported(EFI_DRIVER_BINDING_PROTOCOL *b, EFI_HANDLE h,
                                EFI_DEVICE_PATH_PROTOCOL *path) {
  UINTN role = (UINTN)(b - Bind);
  assert(role < 4 && !path && !Exclusive && h == (role ? CHILD : ROOT));
  return FailSupported == role + 1 ? EFI_UNSUPPORTED : EFI_SUCCESS;
}
EFI_STATUS EFIAPI MockStop(EFI_DRIVER_BINDING_PROTOCOL *b, EFI_HANDLE h, UINTN n,
                           EFI_HANDLE *children) {
  static const unsigned order[] = {3, 2, 1, 0, 0};
  UINTN role = (UINTN)(b - Bind);
  assert(role < 4 && !InNotify && !Exclusive && Stops < 5 && role == order[Stops]);
  assert(h == (role ? CHILD : ROOT));
  if (role == 3) assert(n == 1 && children[0] == CONSOLE && Mask == 63);
  if (role == 2) assert(n == 0 && !children && Mask == 15);
  if (role == 1) assert(n == 0 && !children && Mask == 7);
  if (role == 0 && n) assert(n == 1 && children[0] == CHILD && Mask == 3);
  if (role == 0 && !n) assert(!children && Mask == 1);
  Stops++;
  if (FailStop == Stops) return EFI_DEVICE_ERROR;
  if (role == 3) Mask &= ~48U;
  if (role == 2) Mask &= ~8U;
  if (role == 1) Mask &= ~4U;
  if (role == 0 && n) Mask &= ~2U;
  if (role == 0 && !n) {
    Mask = 0;
    Config[1] &= ~3U;
    if (MasterAfterStop) Config[1] |= 4;
#ifdef GC_LENOVO_VIRTUAL_RELEASE
    if (ChangedVirtualAfterStop) MirrorMode.FrameBufferBase++;
    if (ExtraGopAfterStop) MoreGops = TRUE;
#endif
  }
  if (ChangedInput) Table.ConIn = &OtherInput;
  return EFI_SUCCESS;
}
EFI_STATUS EFIAPI MockStart(EFI_DRIVER_BINDING_PROTOCOL *b, EFI_HANDLE h,
                            EFI_DEVICE_PATH_PROTOCOL *path) {
  UINTN role = (UINTN)(b - Bind);
  static const UINT32 before[] = {0, 3, 7, 15};
  if (role >= 4 || path || InNotify || Exclusive || Mask != before[role])
    printf("Invalid mocked Start: completed_cases=%u role=%u starts=%u mask=%u\n", Tests,
           (unsigned)role, Starts, Mask);
  assert(role < 4 && !path && !InNotify && !Exclusive && Mask == before[role]);
  assert(!(StartRoles & (1U << role)));
  StartRoles |= 1U << role;
  assert(h == (role ? CHILD : ROOT));
  Starts++;
  if (FailStart == Starts) return EFI_DEVICE_ERROR;
  if (role == 0) {
    Mask = 3;
    if (Recreate) ActiveChild = 102;
    memcpy(Config, Initial, sizeof(Config));
  }
  if (role == 1) Mask |= 4;
  if (role == 2) Mask |= 8;
  if (role == 3) Mask |= 48;
  if (AutoMask[role]) Mask = AutoMask[role];
  if (Mask == 63) {
    MirrorMode = Mode;
    MirrorMode.Info = &MirrorInfo;
    if (BadRestoreConfig) Config[4] ^= 0x1000000;
    if (BadRestoreMode) {
      Info.HorizontalResolution = 1280;
      MirrorInfo = Info;
    }
  }
  if (role == 0) {
    if (DriverChangedAfterStart) Bind[DriverChangedAfterStart - 1].Version++;
    if (ChildOwnerAfterStart) ExtraChild = TRUE;
    if (InputAfterStart) Table.ConIn = &OtherInput;
    if (RecordsAfterStart) RecordsError = TRUE;
    if (CountAfterStart) BadCount = TRUE;
  }
  return EFI_SUCCESS;
}
static VOID Setup(VOID) {
  assert(!Allocations && !InNotify);
  memset(&Bs, 0, sizeof(Bs));
  memset(&Table, 0, sizeof(Table));
  memset(&Context, 0, sizeof(Context));
  memset(&Pci, 0, sizeof(Pci));
  memset(Bind, 0, sizeof(Bind));
  memset(Images, 0, sizeof(Images));
  memset(&Mode, 0, sizeof(Mode));
  memset(&Info, 0, sizeof(Info));
  memset(&Gop, 0, sizeof(Gop));
  memset(&Mirror, 0, sizeof(Mirror));
  Stops = Starts = Claims = Closes = DirectWrites = FailStop = FailStart = FailSupported = 0;
  memset(AutoMask, 0, sizeof(AutoMask));
  StartRoles = RecoveryScans = DriverChangedAfterStart = 0;
#ifdef GC_LENOVO_VIRTUAL_RELEASE
  Withdraws = Publishes = Raises = InstallNotifyMask = PendingNotifyMask = 0;
  VirtualHidden = VirtualPublished = FailWithdraw = IgnoreWithdraw = FailPublish =
      ChangePublishHandle = ChangePublishedInterface = CorruptWithdrawnMode = BusyVirtualAfterStop =
          ChangedVirtualAfterStop = ExtraGopAfterStop = RaceBeforeWithdraw = ReappearBeforeClaim =
              ExtraOwnerOnPublish = FALSE;
  FailVirtualLog = NULL;
#endif
  Exclusive = Recreate = ExtraRoot = ExtraChild = BadCount = MoreGops = MirrorMismatch =
      ResidualVirtual = ResidualPhysical = FailClaim = WrongClaim = FailClose = BadRestoreConfig =
          BadRestoreMode = ExtraAfterStop = MasterAfterStop = ChangedInput = FailIntentLog =
              FailResultLog = RecordsError = FALSE;
  ResidualPath = VirtualFramebuffer = IsolationEnumError = FailIsolationLog = FailRecoveryLog =
      OwnerOnRecovery = FALSE;
  OwnerOnNext = ChildOwnerAfterStart = InputAfterStart = FailStateLog = FailRecordLog =
      RecordsAfterStart = CountAfterStart = FALSE;
  Mask = 63;
  ActiveChild = 2;
  static const UINT32 captured[64] = {
#include "../../build/lenovo-cycle-check/captured-config.inc"
  };
  memcpy(Config, captured, sizeof(Config));
  memcpy(Initial, captured, sizeof(Initial));
  UINT8 *bases[] = {TestImage0, TestImage1, TestImage2, TestImage3};
  EFI_DRIVER_BINDING_START starts[] = {TestStart0, TestStart1, TestStart2, TestStart3};
  EFI_DRIVER_BINDING_STOP stops[] = {TestStop0, TestStop1, TestStop2, TestStop3};
  EFI_DRIVER_BINDING_SUPPORTED supported[] = {TestSupported0, TestSupported1, TestSupported2,
                                              TestSupported3};
  UINT64 sizes[] = {193248, 24608, 22016, 35840};
  for (UINTN i = 0; i < 4; i++) {
    Bind[i] = (EFI_DRIVER_BINDING_PROTOCOL){supported[i],      starts[i], stops[i],
                                            i ? 0xa : 0x6000d, H(20 + i), H(10 + i)};
    Images[i].ImageBase = bases[i];
    Images[i].ImageSize = sizes[i];
    Images[i].ImageCodeType = EfiBootServicesCode;
  }
  Pci.Pci.Read = Read;
  Pci.Pci.Write = Write;
  Pci.Attributes = Attributes;
  Mode.MaxMode = 15;
  Mode.SizeOfInfo = sizeof(Info);
  Mode.FrameBufferBase = 0x4000000000ULL;
  Mode.FrameBufferSize = 26214400;
  Mode.Info = &Info;
  Info.HorizontalResolution = 2560;
  Info.VerticalResolution = 1600;
  Info.PixelsPerScanLine = 4096;
  Info.PixelFormat = PixelBlueGreenRedReserved8BitPerColor;
  MirrorInfo = Info;
  MirrorMode = Mode;
  MirrorMode.Info = &MirrorInfo;
  Gop.Mode = &Mode;
  Mirror.Mode = &MirrorMode;
  Mirror.QueryMode = TestGopQuery3;
  Mirror.SetMode = TestGopSet3;
  Mirror.Blt = TestGopBlt3;
  Bs.HandleProtocol = Handle;
  Bs.LocateDevicePath = Locate;
  Bs.LocateHandleBuffer = Handles;
  Bs.ProtocolsPerHandle = Protocols;
  Bs.OpenProtocolInformation = Opens;
  Bs.AllocatePool = Pool;
  Bs.FreePool = FreePool;
  Bs.OpenProtocol = Open;
  Bs.CloseProtocol = Close;
  Bs.RaiseTPL = Raise;
  Bs.RestoreTPL = Restore;
#ifdef GC_LENOVO_VIRTUAL_RELEASE
  Bs.UninstallProtocolInterface = Uninstall;
  Bs.InstallProtocolInterface = Install;
#endif
  Table.BootServices = &Bs;
  Table.ConsoleInHandle = KEYBOARD;
  Table.ConsoleOutHandle = CONSOLE;
  Table.StandardErrorHandle = STDERR_HANDLE;
  Table.ConIn = &Input;
  Table.ConOut = &Output;
  Table.StdErr = &Err;
  Context.table = &Table;
  Context.target = ROOT;
  Context.image = H(99);
  Context.pci = &Pci;
  Context.log = Log;
  Context.identity = 0x24dc10de;
  Context.subsystem = 0x3f8417aa;
  Context.class_revision = 0x030000a1;
  Context.location[1] = 1;
}
#define CHECK(x)                                                                                   \
  do {                                                                                             \
    assert(x);                                                                                     \
    assert(!Allocations && !InNotify && !DirectWrites);                                            \
    Tests++;                                                                                       \
  } while (0)
#define READY()                                                                                    \
  do {                                                                                             \
    Setup();                                                                                       \
    assert(LcPrepare(&Context, &plan) == EFI_SUCCESS);                                             \
  } while (0)
int main(void) {
  LC_PLAN plan;
  GC_RESULT result;
#ifdef LC_LEGACY_REPRO
  READY();
  ResidualPath = TRUE;
  CHECK(LcRun(&Context, &plan, &result) == EFI_ACCESS_DENIED && Stops == 5 && !Starts && !Claims &&
        !result.restored && result.isolated == EFI_ACCESS_DENIED);
  READY();
  ResidualPhysical = TRUE;
  CHECK(LcRun(&Context, &plan, &result) == EFI_ACCESS_DENIED && Stops == 5 && !Starts && !Claims);
  READY();
  ResidualVirtual = VirtualFramebuffer = TRUE;
  CHECK(LcRun(&Context, &plan, &result) == EFI_ACCESS_DENIED && Stops == 5 && !Starts && !Claims);
  printf("PASS %u original v6 refusal/halt reproductions\n", Tests);
  return 0;
#endif
#ifdef LC_V7_REPRO
  for (unsigned i = 0; i < 3; i++) {
    READY();
    ResidualVirtual = VirtualFramebuffer = Recreate = TRUE;
    AutoMask[0] = i == 0 ? 7 : i == 1 ? 15 : 63;
    CHECK(LcRun(&Context, &plan, &result) == EFI_ACCESS_DENIED && Stops == 5 && Starts == 1 &&
          !Claims && !result.restored && result.isolated == EFI_ACCESS_DENIED);
  }
  printf("PASS %u v7 fixed-order recovery guard reproductions; modeled firmware auto-attachment\n",
         Tests);
  return 0;
#endif
  Setup();
  CHECK(LcPrepare(&Context, &plan) == EFI_SUCCESS);
  READY();
  CHECK(LcRun(&Context, &plan, &result) == EFI_SUCCESS && result.restored && Stops == 5 &&
        Starts == 4 && Claims == 1 && Closes == 1);
  READY();
  Recreate = TRUE;
  CHECK(LcRun(&Context, &plan, &result) == EFI_SUCCESS && result.restored && ActiveChild == 102);
  READY();
  ResidualVirtual = TRUE;
  CHECK(LcRun(&Context, &plan, &result) == EFI_SUCCESS && result.restored);
  Setup();
  Context.identity++;
  CHECK(EFI_ERROR(LcPrepare(&Context, &plan)) && !Stops);
  Setup();
  Context.subsystem++;
  CHECK(EFI_ERROR(LcPrepare(&Context, &plan)) && !Stops);
  Setup();
  Context.location[1] = 2;
  CHECK(EFI_ERROR(LcPrepare(&Context, &plan)) && !Stops);
  Setup();
  ExtraRoot = TRUE;
  CHECK(EFI_ERROR(LcPrepare(&Context, &plan)) && !Stops);
  Setup();
  ExtraChild = TRUE;
  CHECK(EFI_ERROR(LcPrepare(&Context, &plan)) && !Stops);
  Setup();
  BadCount = TRUE;
  CHECK(EFI_ERROR(LcPrepare(&Context, &plan)) && !Stops);
  Setup();
  MoreGops = TRUE;
  CHECK(EFI_ERROR(LcPrepare(&Context, &plan)) && !Stops);
  Setup();
  MirrorInfo.HorizontalResolution++;
  CHECK(EFI_ERROR(LcPrepare(&Context, &plan)) && !Stops);
  Setup();
  RecordsError = TRUE;
  CHECK(EFI_ERROR(LcPrepare(&Context, &plan)) && !Stops);
  Setup();
  Config[1] |= 4;
  CHECK(EFI_ERROR(LcPrepare(&Context, &plan)) && !Stops);
  for (unsigned role = 0; role < 4; role++) {
    Setup();
    Bind[role].Version++;
    CHECK(EFI_ERROR(LcPrepare(&Context, &plan)) && !Stops);
    Setup();
    Images[role].ImageSize++;
    CHECK(EFI_ERROR(LcPrepare(&Context, &plan)) && !Stops);
    Setup();
    Images[role].ImageCodeType = EfiRuntimeServicesCode;
    CHECK(EFI_ERROR(LcPrepare(&Context, &plan)) && !Stops);
  }
  READY();
  Config[4] ^= 0x1000000;
  CHECK(EFI_ERROR(LcRun(&Context, &plan, &result)) && !result.touched && !Stops);
  READY();
  Table.ConIn = &OtherInput;
  CHECK(EFI_ERROR(LcRun(&Context, &plan, &result)) && !result.touched && !Stops);
  READY();
  Bind[0].Supported = TestSupported1;
  CHECK(EFI_ERROR(LcRun(&Context, &plan, &result)) && !result.touched && !Stops);
  READY();
  FailIntentLog = TRUE;
  CHECK(EFI_ERROR(LcRun(&Context, &plan, &result)) && !result.touched && !Stops);
  READY();
  FailResultLog = TRUE;
  CHECK(EFI_ERROR(LcRun(&Context, &plan, &result)) && result.touched && Stops == 1 && !Starts);
  for (unsigned i = 1; i <= 5; i++) {
    READY();
    FailStop = i;
    CHECK(EFI_ERROR(LcRun(&Context, &plan, &result)) && result.touched && !result.restored &&
          Stops == i && !Starts);
  }
  for (unsigned i = 1; i <= 4; i++) {
    READY();
    FailStart = i;
    CHECK(EFI_ERROR(LcRun(&Context, &plan, &result)) && result.touched && !result.restored &&
          Starts == i);
  }
  for (unsigned i = 1; i <= 4; i++) {
    READY();
    FailSupported = i;
    CHECK(EFI_ERROR(LcRun(&Context, &plan, &result)) && result.touched && !result.restored &&
          Starts == i - 1);
  }
  READY();
  ExtraAfterStop = TRUE;
  CHECK(EFI_ERROR(LcRun(&Context, &plan, &result)) && result.touched && Stops == 1 && !Starts);
  READY();
  ChangedInput = TRUE;
  CHECK(EFI_ERROR(LcRun(&Context, &plan, &result)) && result.touched && Stops == 1 && !Starts);
  READY();
  MasterAfterStop = TRUE;
  CHECK(EFI_ERROR(LcRun(&Context, &plan, &result)) && result.touched && !Claims && !Starts);
  READY();
  ResidualPhysical = TRUE;
  CHECK(LcRun(&Context, &plan, &result) == EFI_SUCCESS && result.restored && !Claims && !Closes &&
        Starts == 4 && result.isolated == EFI_ACCESS_DENIED);
  READY();
  ResidualPath = TRUE;
  CHECK(LcRun(&Context, &plan, &result) == EFI_SUCCESS && result.restored && !Claims && !Closes &&
        Starts == 4 && result.isolated == EFI_ACCESS_DENIED);
  READY();
  ResidualPath = Recreate = TRUE;
  CHECK(LcRun(&Context, &plan, &result) == EFI_SUCCESS && result.restored && !Claims &&
        ActiveChild == 102 && result.isolated == EFI_ACCESS_DENIED);
  READY();
  ResidualVirtual = VirtualFramebuffer = TRUE;
#ifdef GC_LENOVO_VIRTUAL_RELEASE
  CHECK(LcRun(&Context, &plan, &result) == EFI_SUCCESS && result.restored && Claims == 1 &&
        Closes == 1 && Starts == 4 && result.isolated == EFI_SUCCESS && Withdraws == 1 &&
        Publishes == 1 && !VirtualHidden);
#else
  CHECK(LcRun(&Context, &plan, &result) == EFI_SUCCESS && result.restored && !Claims && !Closes &&
        Starts == 4 && result.isolated == EFI_ACCESS_DENIED);
#endif
  READY();
  ResidualPath = TRUE;
  FailStart = 1;
  CHECK(EFI_ERROR(LcRun(&Context, &plan, &result)) && !result.restored && !Claims && Starts == 1);
  READY();
  ResidualPath = OwnerOnRecovery = TRUE;
  CHECK(EFI_ERROR(LcRun(&Context, &plan, &result)) && !Claims && !Starts && !result.restored);
  READY();
  ResidualPath = FailRecoveryLog = TRUE;
  CHECK(EFI_ERROR(LcRun(&Context, &plan, &result)) && !Claims && !Starts);
  READY();
  ResidualPath = FailIsolationLog = TRUE;
  CHECK(EFI_ERROR(LcRun(&Context, &plan, &result)) && !Claims && !Starts);
  READY();
  IsolationEnumError = TRUE;
  CHECK(EFI_ERROR(LcRun(&Context, &plan, &result)) && !Claims && !Starts);
  READY();
  ResidualPath = BadRestoreConfig = TRUE;
  CHECK(EFI_ERROR(LcRun(&Context, &plan, &result)) && !Claims && Starts == 4 && !result.restored);
  READY();
  ResidualPath = MasterAfterStop = TRUE;
  CHECK(EFI_ERROR(LcRun(&Context, &plan, &result)) && !Claims && !Starts);
  READY();
  FailClaim = TRUE;
  CHECK(EFI_ERROR(LcRun(&Context, &plan, &result)) && Claims == 1 && !Closes && !Starts);
  READY();
  WrongClaim = TRUE;
  CHECK(EFI_ERROR(LcRun(&Context, &plan, &result)) && Claims == 1 && Closes == 1 && !Starts);
  READY();
  FailClose = TRUE;
  CHECK(EFI_ERROR(LcRun(&Context, &plan, &result)) && Claims == 1 && Closes == 1 && !Starts);
  READY();
  BadRestoreConfig = TRUE;
  CHECK(EFI_ERROR(LcRun(&Context, &plan, &result)) && !result.restored && Starts == 4);
  READY();
  BadRestoreMode = TRUE;
  CHECK(EFI_ERROR(LcRun(&Context, &plan, &result)) && !result.restored && Starts == 4);
  for (unsigned i = 0; i < 3; i++) {
    READY();
    ResidualVirtual = VirtualFramebuffer = Recreate = TRUE;
    AutoMask[0] = i == 0 ? 7 : i == 1 ? 15 : 63;
#ifdef GC_LENOVO_VIRTUAL_RELEASE
    CHECK(LcRun(&Context, &plan, &result) == EFI_SUCCESS && result.restored && Stops == 5 &&
          Starts == 3 - i && Claims == 1 && Closes == 1 && ActiveChild == 102 &&
          result.isolated == EFI_SUCCESS && Withdraws == 1 && Publishes == 1 &&
          RecoveryScans == 3 - i);
#else
    CHECK(LcRun(&Context, &plan, &result) == EFI_SUCCESS && result.restored && Stops == 5 &&
          Starts == 3 - i && !Claims && !Closes && ActiveChild == 102 &&
          result.isolated == EFI_ACCESS_DENIED && RecoveryScans == 3 - i);
#endif
  }
  for (unsigned i = 0; i < 2; i++) {
    READY();
    AutoMask[1] = i == 0 ? 15 : 63;
    CHECK(LcRun(&Context, &plan, &result) == EFI_SUCCESS && result.restored && Starts == 3 - i &&
          Claims == 1 && Closes == 1);
  }
  READY();
  AutoMask[2] = 63;
  CHECK(LcRun(&Context, &plan, &result) == EFI_SUCCESS && result.restored && Starts == 3);
  READY();
  AutoMask[0] = 7;
  AutoMask[2] = 63;
  CHECK(LcRun(&Context, &plan, &result) == EFI_SUCCESS && result.restored && Starts == 2 &&
        StartRoles == 5);
  const UINT32 invalid[] = {1, 11, 19, 31, 47};
  for (unsigned i = 0; i < sizeof(invalid) / sizeof(invalid[0]); i++) {
    READY();
    AutoMask[0] = invalid[i];
    CHECK(EFI_ERROR(LcRun(&Context, &plan, &result)) && !result.restored && Starts == 1);
  }
  for (unsigned i = 1; i <= 4; i++) {
    READY();
    AutoMask[0] = 63;
    DriverChangedAfterStart = i;
    CHECK(EFI_ERROR(LcRun(&Context, &plan, &result)) && !result.restored && Starts == 1);
  }
  READY();
  AutoMask[0] = 63;
  ChildOwnerAfterStart = TRUE;
  CHECK(EFI_ERROR(LcRun(&Context, &plan, &result)) && !result.restored && Starts == 1);
  READY();
  AutoMask[0] = 63;
  InputAfterStart = TRUE;
  CHECK(EFI_ERROR(LcRun(&Context, &plan, &result)) && !result.restored && Starts == 1);
  READY();
  AutoMask[0] = 63;
  CountAfterStart = TRUE;
  CHECK(EFI_ERROR(LcRun(&Context, &plan, &result)) && !result.restored && Starts == 1);
  READY();
  RecordsAfterStart = TRUE;
  CHECK(EFI_ERROR(LcRun(&Context, &plan, &result)) && !result.restored && Starts == 1);
  READY();
  AutoMask[0] = 63;
  BadRestoreConfig = TRUE;
  CHECK(EFI_ERROR(LcRun(&Context, &plan, &result)) && !result.restored && Starts == 1);
  READY();
  AutoMask[0] = 63;
  BadRestoreMode = TRUE;
  CHECK(EFI_ERROR(LcRun(&Context, &plan, &result)) && !result.restored && Starts == 1);
  READY();
  FailStateLog = TRUE;
  CHECK(EFI_ERROR(LcRun(&Context, &plan, &result)) && !result.restored && Starts == 1);
  READY();
  FailRecordLog = TRUE;
  CHECK(EFI_ERROR(LcRun(&Context, &plan, &result)) && !result.restored && Starts == 1);
  READY();
  OwnerOnNext = TRUE;
  CHECK(EFI_ERROR(LcRun(&Context, &plan, &result)) && !result.restored && Starts == 1);
  READY();
  AutoMask[1] = 3;
  CHECK(EFI_ERROR(LcRun(&Context, &plan, &result)) && !result.restored && Starts == 2);
#ifdef GC_LENOVO_VIRTUAL_RELEASE
#define VREADY()                                                                                   \
  do {                                                                                             \
    READY();                                                                                       \
    ResidualVirtual = VirtualFramebuffer = TRUE;                                                   \
  } while (0)
  for (unsigned i = 0; i < 4; i++) {
    VREADY();
    InstallNotifyMask = i == 0 ? 3 : i == 1 ? 7 : i == 2 ? 15 : 63;
    CHECK(LcRun(&Context, &plan, &result) == EFI_SUCCESS && result.restored && Starts == 3 - i &&
          Claims == 1 && Withdraws == 1 && Publishes == 1 && !(StartRoles & 1));
  }
  Setup();
  Bs.UninstallProtocolInterface = NULL;
  CHECK(EFI_ERROR(LcPrepare(&Context, &plan)) && !Stops);
  Setup();
  Bs.InstallProtocolInterface = NULL;
  CHECK(EFI_ERROR(LcPrepare(&Context, &plan)) && !Stops);
  Setup();
  Mirror.QueryMode = NULL;
  CHECK(EFI_ERROR(LcPrepare(&Context, &plan)) && !Stops);
  Setup();
  Mirror.SetMode = (EFI_GRAPHICS_OUTPUT_PROTOCOL_SET_MODE)(UINTN)TestImage1;
  CHECK(EFI_ERROR(LcPrepare(&Context, &plan)) && !Stops);
  Setup();
  Mirror.Blt = (EFI_GRAPHICS_OUTPUT_PROTOCOL_BLT)(UINTN)TestImage1;
  CHECK(EFI_ERROR(LcPrepare(&Context, &plan)) && !Stops);
  VREADY();
  FailWithdraw = TRUE;
  CHECK(EFI_ERROR(LcRun(&Context, &plan, &result)) && !result.restored && Withdraws == 1 &&
        !Publishes && !Claims && !Starts);
  VREADY();
  IgnoreWithdraw = TRUE;
  CHECK(EFI_ERROR(LcRun(&Context, &plan, &result)) && !result.restored && Withdraws == 1 &&
        !Publishes && !Claims && !Starts);
  VREADY();
  FailPublish = TRUE;
  CHECK(EFI_ERROR(LcRun(&Context, &plan, &result)) && !result.restored && Withdraws == 1 &&
        Publishes == 1 && Claims == 1 && Closes == 1 && !Starts);
  VREADY();
  ChangePublishHandle = TRUE;
  CHECK(EFI_ERROR(LcRun(&Context, &plan, &result)) && !result.restored && Publishes == 1 &&
        !Starts);
  VREADY();
  ChangePublishedInterface = TRUE;
  CHECK(EFI_ERROR(LcRun(&Context, &plan, &result)) && !result.restored && Publishes == 1 &&
        !Starts);
  VREADY();
  CorruptWithdrawnMode = TRUE;
  CHECK(EFI_ERROR(LcRun(&Context, &plan, &result)) && !result.restored && Withdraws == 1 &&
        !Publishes && !Claims && !Starts);
  VREADY();
  BusyVirtualAfterStop = TRUE;
  CHECK(EFI_ERROR(LcRun(&Context, &plan, &result)) && !result.restored && !Withdraws && !Claims &&
        !Starts);
  VREADY();
  ChangedVirtualAfterStop = TRUE;
  CHECK(LcRun(&Context, &plan, &result) == EFI_SUCCESS && result.restored && !Withdraws &&
        !Claims && Starts == 4 && result.isolated == EFI_ACCESS_DENIED);
  VREADY();
  ExtraGopAfterStop = TRUE;
  CHECK(EFI_ERROR(LcRun(&Context, &plan, &result)) && !result.restored && !Withdraws && !Claims);
  VREADY();
  RaceBeforeWithdraw = TRUE;
  CHECK(EFI_ERROR(LcRun(&Context, &plan, &result)) && !result.restored && !Withdraws && !Claims &&
        !Starts);
  VREADY();
  ReappearBeforeClaim = TRUE;
  CHECK(EFI_ERROR(LcRun(&Context, &plan, &result)) && !result.restored && Withdraws == 1 &&
        !Claims && !Starts);
  VREADY();
  ExtraOwnerOnPublish = TRUE;
  CHECK(EFI_ERROR(LcRun(&Context, &plan, &result)) && !result.restored && Publishes == 1 &&
        !Starts);
  const char *fail_logs[] = {"LC_VIRTUAL_WITHDRAW_INTENT", "LC_VIRTUAL_WITHDRAW_RESULT",
                             "LC_VIRTUAL_RESTORE_INTENT", "LC_VIRTUAL_RESTORE_RESULT"};
  for (unsigned i = 0; i < 4; i++) {
    VREADY();
    FailVirtualLog = fail_logs[i];
    CHECK(EFI_ERROR(LcRun(&Context, &plan, &result)) && !result.restored && !Starts &&
          Withdraws == (i ? 1 : 0) && Publishes == (i == 3 ? 1 : 0));
  }
#endif
  printf("PASS %u Lenovo lifecycle cases; exact driver offsets; no direct GPU writes or reset\n",
         Tests);
  return 0;
}
