/* Production display, ownership, single-owner release and independent flow.
   GPU transaction/firmware and pre/post register checks are modeled here. */
#define main DisplayFixtureMain
#define GuLog DisplayFixtureLog
#include "test_console_mirror.c"
#undef GuLog
#undef main
#include "../single_owner.h"
#include "../../gop/patch.h"
#include <Protocol/LoadedImage.h>
#include <assert.h>

static GC_CONTEXT Context;
static GC_CONTEXT *PhaseContext;
static BOOLEAN PatchIo;
static UINT32 PreservedSs1;
static EFI_GUID BindingId = EFI_DRIVER_BINDING_PROTOCOL_GUID;
static EFI_GUID LoadedId = EFI_LOADED_IMAGE_PROTOCOL_GUID;
static EFI_GUID ExtraId = {0x4006c0c1, 0, 0, {0}};
static EFI_DRIVER_BINDING_PROTOCOL Binding;
static EFI_LOADED_IMAGE_PROTOCOL OwnerImage;
static EFI_PCI_IO_PROTOCOL Replacement;
static UINT32 ConfigWords[64];
static BOOLEAN Bound, Claimed, Child, Multiple, WrongController, WrongCount;
static BOOLEAN FailDetach, NoopDetach, MissingPci, ReplacedPci, BadIdentity, Rebind;
static BOOLEAN FailClaim, WrongClaimInterface, FailClose, FailPatch, FailObserve;
static BOOLEAN FailConfig, FailCold, FailProm, FailBar, ChangeDisplay;
static BOOLEAN FailIntentLog, FailResultLog;
static unsigned Detaches, Claims, Patches, Observes, Closes, Passed;
static EFI_HANDLE Agent;
#define OWNER_HANDLE ((EFI_HANDLE)(UINTN)0x30)
#define OTHER_OWNER ((EFI_HANDLE)(UINTN)0x31)
#define APP_IMAGE ((EFI_HANDLE)(UINTN)0x40)

VOID GuLog(const char *format, ...) {
  if ((FailIntentLog && strstr(format, "SINGLE_OWNER_RELEASE_INTENT")) ||
      (FailResultLog && strstr(format, "SINGLE_OWNER_RELEASE_RESULT")))
    LogOk = FALSE;
}
static EFI_TPL EFIAPI Raise(EFI_TPL tpl) {
  (void)tpl;
  return TPL_APPLICATION;
}
static VOID EFIAPI Restore(EFI_TPL tpl) { (void)tpl; }
static EFI_STATUS EFIAPI NeverStop(EFI_DRIVER_BINDING_PROTOCOL *self, EFI_HANDLE controller,
                                   UINTN n, EFI_HANDLE *children) {
  (void)self;
  (void)controller;
  (void)n;
  (void)children;
  abort();
}
static EFI_STATUS EFIAPI ConfigRead(EFI_PCI_IO_PROTOCOL *pci, EFI_PCI_IO_PROTOCOL_WIDTH width,
                                    UINT32 offset, UINTN count, VOID *data) {
  if (pci == &TestIntel) return TestRead(pci, width, offset, count, data);
  assert(pci == &TestTarget || pci == &Replacement);
  assert(width == EfiPciIoWidthUint32 && offset % 4 == 0 && offset + count * 4 <= 256);
  memcpy(data, (UINT8 *)ConfigWords + offset, count * 4);
  return EFI_SUCCESS;
}
static EFI_STATUS EFIAPI NeverWrite(EFI_PCI_IO_PROTOCOL *pci, EFI_PCI_IO_PROTOCOL_WIDTH width,
                                    UINT32 offset, UINTN count, VOID *data) {
  (void)pci;
  (void)width;
  (void)offset;
  (void)count;
  (void)data;
  abort();
}
static EFI_STATUS EFIAPI Handle(EFI_HANDLE h, EFI_GUID *id, VOID **out) {
  if (h == Agent && !memcmp(id, &BindingId, sizeof(*id))) {
    *out = &Binding;
    return EFI_SUCCESS;
  }
  if (h == Binding.ImageHandle && !memcmp(id, &LoadedId, sizeof(*id))) {
    *out = &OwnerImage;
    return EFI_SUCCESS;
  }
  if (h == TARGET_HANDLE && !memcmp(id, &PciGuid, sizeof(*id)) && Detaches) {
    *out = MissingPci ? NULL : ReplacedPci ? &Replacement : &TestTarget;
    return *out ? EFI_SUCCESS : EFI_NOT_FOUND;
  }
  return TestHandle(h, id, out);
}
static EFI_STATUS EFIAPI Protocols(EFI_HANDLE h, EFI_GUID ***ids, UINTN *n) {
  assert(h == TARGET_HANDLE);
  *n = 3;
  *ids = TestAlloc(*n * sizeof(**ids));
  (*ids)[0] = &PciGuid;
  (*ids)[1] = &PathGuid;
  (*ids)[2] = &ExtraId;
  return EFI_SUCCESS;
}
static EFI_STATUS EFIAPI Opens(EFI_HANDLE h, EFI_GUID *id,
                               EFI_OPEN_PROTOCOL_INFORMATION_ENTRY **entries, UINTN *n) {
  assert(h == TARGET_HANDLE);
  *n = 1;
  BOOLEAN pci = !memcmp(id, &PciGuid, sizeof(*id));
  if (pci && (Bound || Claimed)) (*n)++;
  if (pci && Bound && Multiple) (*n)++;
  if (Child && !memcmp(id, &ExtraId, sizeof(*id))) (*n)++;
  *entries = TestAlloc(*n * sizeof(**entries));
  EFI_OPEN_PROTOCOL_INFORMATION_ENTRY *e = *entries;
  e[0].Attributes = EFI_OPEN_PROTOCOL_GET_PROTOCOL;
  e[0].AgentHandle = OTHER_OWNER;
  e[0].OpenCount = 5;
  if (pci && (Bound || Claimed)) {
    e[1].Attributes = Claimed ? EFI_OPEN_PROTOCOL_EXCLUSIVE : EFI_OPEN_PROTOCOL_BY_DRIVER;
    e[1].AgentHandle = Claimed ? APP_IMAGE : Agent;
    e[1].ControllerHandle = Claimed ? NULL : WrongController ? H(3) : TARGET_HANDLE;
    e[1].OpenCount = WrongCount ? 2 : 1;
    if (Bound && Multiple) {
      e[2] = e[1];
      e[2].AgentHandle = OTHER_OWNER;
    }
  }
  if (Child && !memcmp(id, &ExtraId, sizeof(*id))) {
    e[1].Attributes = EFI_OPEN_PROTOCOL_BY_CHILD_CONTROLLER;
    e[1].AgentHandle = Agent;
    e[1].ControllerHandle = H(3);
    e[1].OpenCount = 1;
  }
  return EFI_SUCCESS;
}
static EFI_STATUS EFIAPI Disconnect(EFI_HANDLE target, EFI_HANDLE driver, EFI_HANDLE child) {
  assert(target == TARGET_HANDLE && driver == Agent && !child && !Claimed);
  assert(!Gu.pci && Gu.armed && SingleOwnerPlan.consumed);
  Detaches++;
  if (!NoopDetach) Bound = FALSE;
  if (Rebind) {
    Bound = TRUE;
    Agent = OTHER_OWNER;
  }
  if (BadIdentity) ConfigWords[0] ^= 0x10000;
  if (ChangeDisplay) TargetRoute = TRUE;
  return FailDetach ? EFI_DEVICE_ERROR : EFI_SUCCESS;
}
static EFI_STATUS EFIAPI Claim(EFI_HANDLE h, EFI_GUID *id, VOID **out, EFI_HANDLE image,
                               EFI_HANDLE controller, UINT32 attr) {
  assert(h == TARGET_HANDLE && !memcmp(id, &PciGuid, sizeof(*id)) && image == APP_IMAGE &&
         !controller && attr == EFI_OPEN_PROTOCOL_EXCLUSIVE && !Bound && !Claimed);
  Claims++;
  if (FailClaim) return EFI_ACCESS_DENIED;
  Claimed = TRUE;
  *out = WrongClaimInterface ? &Replacement : &TestTarget;
  return EFI_SUCCESS;
}
static EFI_STATUS EFIAPI Close(EFI_HANDLE h, EFI_GUID *id, EFI_HANDLE image,
                               EFI_HANDLE controller) {
  assert(h == TARGET_HANDLE && !memcmp(id, &PciGuid, sizeof(*id)) && image == APP_IMAGE &&
         !controller && Claimed);
  Closes++;
  if (FailClose) return EFI_DEVICE_ERROR;
  Claimed = FALSE;
  return EFI_SUCCESS;
}
/* Exact target identity predicate from production port.c, copied by the runner. */
#include "../../../build/single-owner-check/target-under-test.c"
static EFI_STATUS ConfigCheck(BOOLEAN save) {
  assert(!save);
  return FailConfig ? EFI_DEVICE_ERROR : EFI_SUCCESS;
}
static EFI_STATUS BarCheck(VOID) { return FailBar ? EFI_DEVICE_ERROR : EFI_SUCCESS; }
static EFI_STATUS ColdCheck(VOID) { return FailCold ? EFI_DEVICE_ERROR : EFI_SUCCESS; }
static EFI_STATUS PromCheck(VOID) { return FailProm ? EFI_DEVICE_ERROR : EFI_SUCCESS; }
static BOOLEAN ContextLog(const char *format, ...) {
  (void)format;
  return LogOk;
}
BOOLEAN GxSafe(VOID) { return TRUE; }
EFI_STATUS GxPatch(BOOLEAN (*isolated)(VOID)) {
  assert(Claimed && !Bound && Gu.owned && isolated());
  Patches++;
  return FailPatch ? EFI_DEVICE_ERROR : EFI_SUCCESS;
}
EFI_STATUS GxObserve(GC_CONTEXT *context, const char *phase) {
  (void)phase;
  assert(context == &Context && Patches == 1);
  Observes++;
  return FailObserve ? EFI_DEVICE_ERROR : EFI_SUCCESS;
}
VOID GxReleased(VOID) { Gu.owned = Gu.armed = Gu.display_ok = FALSE; }
/* Actual production IndependentPrepare/Run, including the new one-owner flow. */
#include "../../../build/single-owner-check/flow-under-test.c"

static VOID Reset(VOID) {
  Setup();
  memset(&Context, 0, sizeof(Context));
  memset(&SingleOwnerPlan, 0, sizeof(SingleOwnerPlan));
  memset(&GuProfile, 0, sizeof(GuProfile));
  memset(&Binding, 0, sizeof(Binding));
  memset(&OwnerImage, 0, sizeof(OwnerImage));
  memset(ConfigWords, 0, sizeof(ConfigWords));
  Bound = TRUE;
  Claimed = Child = Multiple = WrongController = WrongCount = FALSE;
  FailDetach = NoopDetach = MissingPci = ReplacedPci = BadIdentity = Rebind = FALSE;
  FailClaim = WrongClaimInterface = FailClose = FailPatch = FailObserve = FALSE;
  FailConfig = FailCold = FailProm = FailBar = ChangeDisplay = FALSE;
  FailIntentLog = FailResultLog = FALSE;
  Detaches = Claims = Patches = Observes = Closes = 0;
  Agent = OWNER_HANDLE;
  Binding.DriverBindingHandle = Agent;
  Binding.ImageHandle = Agent;
  Binding.Stop = NeverStop;
  Binding.Version = 0x20;
  OwnerImage.ImageBase = (VOID *)((UINTN)NeverStop - 0xa64);
  OwnerImage.ImageSize = 7200;
  OwnerImage.ImageCodeType = EfiBootServicesCode;
  GuProfile.identity = ConfigWords[0] = 0x24dc10de;
  GuProfile.subsystem = ConfigWords[11] = 0x3f8417aa;
  GuProfile.chip = 0xb74000a1;
  GuProfile.fb_mib = 0x4000;
  ConfigWords[2] = 0x030000a1;
  TestTarget.Pci.Read = ConfigRead;
  TestTarget.Pci.Write = NeverWrite;
  Replacement = TestTarget;
  TestBs.RaiseTPL = Raise;
  TestBs.RestoreTPL = Restore;
  TestBs.HandleProtocol = Handle;
  TestBs.ProtocolsPerHandle = Protocols;
  TestBs.OpenProtocolInformation = Opens;
  TestBs.DisconnectController = Disconnect;
  TestBs.OpenProtocol = Claim;
  TestBs.CloseProtocol = Close;
  Gu.image = APP_IMAGE;
  Context.table = &TestTable;
  Context.pci = &TestTarget;
  Context.target = TARGET_HANDLE;
  Context.image = APP_IMAGE;
  Context.log = ContextLog;
  PhaseContext = &Context;
  PatchIo = TRUE;
  PreservedSs1 = 6;
}
static VOID Prepared(VOID) {
  assert(GxIndependentPrepare(&Context) == EFI_SUCCESS);
  assert(!Detaches && !Claims && !Patches && !LiveAllocations);
}
static VOID DeniedPrepare(const char *name) {
  assert(EFI_ERROR(GxIndependentPrepare(&Context)));
  assert(!Detaches && !Claims && !Patches && !LiveAllocations);
  printf("PASS prepare refuses %s\n", name);
  Passed++;
}
static VOID DeniedRun(const char *name, unsigned detaches, unsigned patches) {
  GC_RESULT r = {0};
  assert(EFI_ERROR(GxIndependentRun(&Context, &r)) && !r.restored);
  assert(Detaches == detaches && Patches == patches && !LiveAllocations);
  if (Detaches || Gu.owned) assert(r.touched);
  printf("PASS execution stops on %s\n", name);
  Passed++;
}
int main(int argc, char **argv) {
  (void)argc;
  (void)argv;
  Reset();
  Prepared();
  GC_RESULT r = {0};
  assert(GxIndependentRun(&Context, &r) == EFI_SUCCESS && r.touched && r.restored);
  assert(Detaches == 1 && Claims == 1 && Patches == 1 && Observes == 1 && Closes == 1);
  assert(!Bound && !Claimed && !Gu.owned && !LiveAllocations);
  Passed++;
  assert(EFI_ERROR(GxIndependentRun(&Context, &r)) && Detaches == 1 && Patches == 1);
  Passed++;
  Reset();
  Bound = FALSE;
  Prepared();
  assert(GxIndependentRun(&Context, &r) == EFI_SUCCESS && !Detaches && Patches == 1);
  Passed++;
  Reset();
  GuProfile.identity ^= 0x10000;
  DeniedPrepare("different GPU");
  Reset();
  GuProfile.subsystem++;
  DeniedPrepare("different subsystem");
  Reset();
  GuProfile.chip++;
  DeniedPrepare("different chip");
  Reset();
  GuProfile.fb_mib /= 2;
  DeniedPrepare("different VRAM");
  Reset();
  Binding.Version++;
  DeniedPrepare("different driver version");
  Reset();
  OwnerImage.ImageSize++;
  DeniedPrepare("different driver size");
  Reset();
  OwnerImage.ImageBase = (UINT8 *)OwnerImage.ImageBase + 1;
  DeniedPrepare("different Stop offset");
  Reset();
  Binding.Stop = NULL;
  DeniedPrepare("missing Stop");
  Reset();
  OwnerImage.ImageCodeType = EfiRuntimeServicesCode;
  DeniedPrepare("runtime driver");
  Reset();
  Multiple = TRUE;
  DeniedPrepare("multiple owners");
  Reset();
  Child = TRUE;
  DeniedPrepare("child on unrelated protocol");
  Reset();
  WrongController = TRUE;
  DeniedPrepare("different controller");
  Reset();
  WrongCount = TRUE;
  DeniedPrepare("unexpected open count");
  Reset();
  TargetRoute = TRUE;
  DeniedPrepare("target GPU owns display");
  Reset();
  LogOk = FALSE;
  DeniedPrepare("logging unavailable");
  Reset();
  PreservedSs1 = 16;
  DeniedPrepare("selector outside supported range");
  Reset();
  DeniedRun("missing approved plan", 0, 0);
  Reset();
  Prepared();
  Binding.Version++;
  DeniedRun("driver changed after plan", 0, 0);
  Reset();
  Prepared();
  Bound = FALSE;
  DeniedRun("owner disappeared after plan", 0, 0);
  Reset();
  Prepared();
  Child = TRUE;
  DeniedRun("child appeared after plan", 0, 0);
  Reset();
  Prepared();
  FailConfig = TRUE;
  DeniedRun("config changed before release", 0, 0);
  Reset();
  Prepared();
  FailBar = TRUE;
  DeniedRun("BAR changed before release", 0, 0);
  Reset();
  Prepared();
  FailCold = TRUE;
  DeniedRun("cold state changed before release", 0, 0);
  Reset();
  Prepared();
  FailProm = TRUE;
  DeniedRun("PROM changed before release", 0, 0);
  Reset();
  Prepared();
  TargetRoute = TRUE;
  DeniedRun("display changed before release", 0, 0);
  Reset();
  Prepared();
  FailIntentLog = TRUE;
  DeniedRun("intent log failure", 0, 0);
  Reset();
  Prepared();
  FailDetach = TRUE;
  DeniedRun("Stop error even after state change", 1, 0);
  Reset();
  Prepared();
  NoopDetach = TRUE;
  DeniedRun("Stop success without removing owner", 1, 0);
  Reset();
  Prepared();
  Rebind = TRUE;
  DeniedRun("new owner after release", 1, 0);
  Reset();
  Prepared();
  MissingPci = TRUE;
  DeniedRun("PCI protocol disappeared", 1, 0);
  Reset();
  Prepared();
  ReplacedPci = TRUE;
  DeniedRun("PCI interface replaced", 1, 0);
  Reset();
  Prepared();
  BadIdentity = TRUE;
  DeniedRun("PCI identity changed", 1, 0);
  Reset();
  Prepared();
  ChangeDisplay = TRUE;
  DeniedRun("display changed during release", 1, 0);
  Reset();
  Prepared();
  FailResultLog = TRUE;
  DeniedRun("release result log failure", 1, 0);
  Reset();
  Prepared();
  FailClaim = TRUE;
  DeniedRun("exclusive acquisition failed", 1, 0);
  Reset();
  Prepared();
  WrongClaimInterface = TRUE;
  DeniedRun("exclusive interface mismatch", 1, 0);
  Reset();
  Prepared();
  FailPatch = TRUE;
  DeniedRun("transaction error", 1, 1);
  Reset();
  Prepared();
  FailObserve = TRUE;
  DeniedRun("selector verification error", 1, 1);
  Reset();
  Prepared();
  FailClose = TRUE;
  DeniedRun("exclusive close failure", 1, 1);
  printf("PASS %u single-owner flow scenarios. GPU operations modeled; no hardware called.\n",
         Passed);
  return 0;
}
