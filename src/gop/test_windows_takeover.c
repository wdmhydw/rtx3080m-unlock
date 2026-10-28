#define LC_TAKEOVER_TEST
#include "test_lenovo_unlock.c"

int main(void) {
  LC_PLAN plan;
  GC_RESULT result;
  UREADY();
  CHECK(LcRun(&Context, &plan, &result) == EFI_SUCCESS && result.windows_ready &&
        result.unlock_verified && !result.restored && Stops == 5 && Patches == 1 &&
        Observations == 2 && Closes == 1 && Releases == 1 && VirtualHidden && !Exclusive && !Mask &&
        !Publishes && !Starts && Selector1 == 6);
  /* A firmware driver that fails on restart is never called in this mode. */
  UREADY();
  FailStart = 1;
  FailPublish = TRUE;
  CHECK(LcRun(&Context, &plan, &result) == EFI_SUCCESS && result.windows_ready && !Starts &&
        !Publishes);
  UREADY();
  AfterReleaseMutation = 8;
  CHECK(LcRun(&Context, &plan, &result) == EFI_SUCCESS && result.windows_ready);
  for (unsigned i = 9; i <= 10; i++) {
    UREADY();
    AfterReleaseMutation = i;
    CHECK(EFI_ERROR(LcRun(&Context, &plan, &result)) && !result.windows_ready && !Starts);
  }
  for (unsigned i = 1; i <= 7; i++) {
    UREADY();
    AfterReleaseMutation = i;
    CHECK(EFI_ERROR(LcRun(&Context, &plan, &result)) && !result.windows_ready &&
          !result.unlock_verified && !Starts && !Publishes && Releases == 1);
  }
  for (unsigned i = 1; i <= 6; i++) {
    UREADY();
    IsolationMutation = i;
    CHECK(EFI_ERROR(LcRun(&Context, &plan, &result)) && !result.windows_ready && !Starts &&
          !Publishes && !Closes);
  }
  for (unsigned i = 1; i <= 5; i++) {
    UREADY();
    FailStop = i;
    CHECK(EFI_ERROR(LcRun(&Context, &plan, &result)) && !result.windows_ready && !Patches &&
          !Starts && !Publishes);
  }
  for (unsigned i = 1; i <= 2; i++) {
    UREADY();
    ObserveError = i;
    CHECK(EFI_ERROR(LcRun(&Context, &plan, &result)) && !result.windows_ready &&
          !result.unlock_verified && !Starts && !Publishes);
  }
  UREADY();
  PatchError = EFI_DEVICE_ERROR;
  CHECK(EFI_ERROR(LcRun(&Context, &plan, &result)) && !result.windows_ready && !Closes);
  UREADY();
  UnsafePatch = TRUE;
  CHECK(EFI_ERROR(LcRun(&Context, &plan, &result)) && !result.windows_ready && !Closes);
  UREADY();
  FailClose = TRUE;
  CHECK(EFI_ERROR(LcRun(&Context, &plan, &result)) && !result.windows_ready && !Releases);
  UREADY();
  FailVirtualLog = "LC_WINDOWS_TAKEOVER_READY";
  CHECK(EFI_ERROR(LcRun(&Context, &plan, &result)) && !result.windows_ready && !Starts);
  UREADY();
  ResidualPath = TRUE;
  CHECK(LcRun(&Context, &plan, &result) == EFI_SUCCESS && result.restored &&
        !result.windows_ready && !result.unlock_attempted && !Patches && Starts == 1);
  printf("PASS %u Windows display takeover cases; firmware GOP remains absent; OS behavior pending "
         "hardware test\n",
         Tests);
  return 0;
}
