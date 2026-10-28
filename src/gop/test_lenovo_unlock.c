/* Run the real Lenovo lifecycle against the captured firmware model, replacing
   only the already-tested unlock transaction at its hardware boundary. */
#define main DiagnosticLifecycleMain
#include "test_lenovo_cycle.c"
#undef main

static unsigned Patches, Observations, Releases;
static EFI_STATUS PatchError;
static unsigned ObserveError, IsolationMutation, AfterReleaseMutation;
static BOOLEAN UnsafePatch;
static UINT32 Selector0, Selector1;
BOOLEAN GxSafe(VOID) { return !UnsafePatch; }
VOID GxReleased(VOID) {
  assert(!Exclusive && Closes == 1 && !Publishes && !Starts);
  Releases++;
  if (AfterReleaseMutation == 1) ExtraRoot = TRUE;
  if (AfterReleaseMutation == 2) VirtualHidden = FALSE;
  if (AfterReleaseMutation == 3) Config[4] ^= 0x1000000;
  if (AfterReleaseMutation == 4) Config[1] |= 4;
  if (AfterReleaseMutation == 5) Table.ConIn = &OtherInput;
  if (AfterReleaseMutation == 6) MirrorMode.FrameBufferBase++;
  if (AfterReleaseMutation == 7) UnsafePatch = TRUE;
  if (AfterReleaseMutation == 8) Config[0x80 / 4] &= 0xffff;
  if (AfterReleaseMutation == 9) Config[0x80 / 4] |= 0x00200000;
  if (AfterReleaseMutation == 10) Config[0xa0 / 4] ^= 1;
}
EFI_STATUS GxPatch(BOOLEAN (*isolated)(VOID)) {
  assert(Stops == 5 && Exclusive && !Mask && !InNotify && !Starts && !Publishes);
  assert(isolated());
  Patches++;
  if (IsolationMutation == 1) ExtraRoot = TRUE;
  if (IsolationMutation == 2) VirtualHidden = FALSE;
  if (IsolationMutation == 3) Table.ConIn = &OtherInput;
  if (IsolationMutation == 4) Bind[0].Supported = TestSupported1;
  if (IsolationMutation == 5) Exclusive = FALSE;
  if (IsolationMutation == 6) MirrorMode.FrameBufferBase++;
  if (!isolated()) return EFI_ACCESS_DENIED;
  if (EFI_ERROR(PatchError)) return PatchError;
  Selector0 = 0x88888888;
  Config[1] |= 3; /* The real patch restores original IO/MEM decode before FLR. */
  return EFI_SUCCESS;
}
EFI_STATUS GxObserve(GC_CONTEXT *c, const char *phase) {
  assert(c == &Context && Selector0 == 0x88888888 && Selector1 == 6);
  Observations++;
  if (!strcmp(phase, "before-gop"))
    assert(Exclusive && !Mask && !Closes && !Starts && !Publishes);
  else if (!strcmp(phase, "windows-takeover")) {
    assert(!Exclusive && !Mask && !Starts && !Publishes && Releases == 1 && VirtualHidden);
  } else {
    assert(!strcmp(phase, "after-gop"));
    assert(!Exclusive && Mask == 63 && Releases == 1);
  }
  return ObserveError == Observations ? EFI_COMPROMISED_DATA : EFI_SUCCESS;
}
static VOID UnlockSetup(LC_PLAN *p) {
  Setup();
  Patches = Observations = Releases = ObserveError = IsolationMutation = 0;
  AfterReleaseMutation = 0;
  PatchError = EFI_SUCCESS;
  UnsafePatch = FALSE;
  Selector0 = 0x66666666;
  Selector1 = 6;
  ResidualVirtual = VirtualFramebuffer = Recreate = TRUE;
  AutoMask[0] = 63;
  assert(LcPrepare(&Context, p) == EFI_SUCCESS);
}
#define UREADY() UnlockSetup(&plan)
#ifndef LC_TAKEOVER_TEST
int main(void) {
  LC_PLAN plan;
  GC_RESULT result;
  UREADY();
  CHECK(LcRun(&Context, &plan, &result) == EFI_SUCCESS && result.restored &&
        result.unlock_verified && Patches == 1 && Observations == 2 && Releases == 1 &&
        Starts == 1 && Withdraws == 1 && Publishes == 1 && Selector1 == 6);
  for (unsigned i = 0; i < 4; i++) {
    UREADY();
    AutoMask[0] = i == 0 ? 3 : i == 1 ? 7 : i == 2 ? 15 : 63;
    CHECK(LcRun(&Context, &plan, &result) == EFI_SUCCESS && result.unlock_verified &&
          Patches == 1 && Starts == 4 - i);
    UREADY();
    InstallNotifyMask = i == 0 ? 3 : i == 1 ? 7 : i == 2 ? 15 : 63;
    CHECK(LcRun(&Context, &plan, &result) == EFI_SUCCESS && result.unlock_verified &&
          Patches == 1 && Starts == 3 - i && !(StartRoles & 1));
  }
  for (unsigned i = 1; i <= 6; i++) {
    UREADY();
    IsolationMutation = i;
    CHECK(EFI_ERROR(LcRun(&Context, &plan, &result)) && !result.restored && Patches == 1 &&
          !Observations && !Closes && !Publishes && !Starts);
  }
  UREADY();
  PatchError = EFI_DEVICE_ERROR;
  CHECK(EFI_ERROR(LcRun(&Context, &plan, &result)) && result.unlock_attempted &&
        !result.unlock_verified && !Observations && !Closes && !Starts);
  UREADY();
  UnsafePatch = TRUE;
  CHECK(EFI_ERROR(LcRun(&Context, &plan, &result)) && Patches == 1 && !Observations && !Closes);
  UREADY();
  ObserveError = 1;
  CHECK(EFI_ERROR(LcRun(&Context, &plan, &result)) && !Closes && !Publishes && !Starts);
  UREADY();
  ObserveError = 2;
  CHECK(EFI_ERROR(LcRun(&Context, &plan, &result)) && !result.restored && !result.unlock_verified &&
        Closes == 1 && Releases == 1 && Starts == 1);
  UREADY();
  FailClose = TRUE;
  CHECK(EFI_ERROR(LcRun(&Context, &plan, &result)) && Patches == 1 && !Releases && !Starts);
  UREADY();
  WrongClaim = TRUE;
  CHECK(EFI_ERROR(LcRun(&Context, &plan, &result)) && !Patches && !Closes && !Starts);
  UREADY();
  FailWithdraw = TRUE;
  CHECK(EFI_ERROR(LcRun(&Context, &plan, &result)) && !Patches && !Claims && !Starts);
  UREADY();
  ReappearBeforeClaim = TRUE;
  CHECK(EFI_ERROR(LcRun(&Context, &plan, &result)) && !Patches && !Claims && !Starts);
  UREADY();
  FailPublish = TRUE;
  CHECK(EFI_ERROR(LcRun(&Context, &plan, &result)) && Patches == 1 && Releases == 1 && !Starts);
  UREADY();
  ResidualPath = TRUE;
  CHECK(LcRun(&Context, &plan, &result) == EFI_SUCCESS && result.restored &&
        !result.unlock_attempted && !result.unlock_verified && !Patches && !Claims);
  UREADY();
  BadRestoreConfig = TRUE;
  CHECK(EFI_ERROR(LcRun(&Context, &plan, &result)) && !result.restored && !result.unlock_verified &&
        Patches == 1 && Observations == 1);
  printf("PASS %u integrated Lenovo unlock ordering and failure cases (modeled GPU transaction)\n",
         Tests);
  return 0;
}
#endif
