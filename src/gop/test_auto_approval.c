#include <Uefi.h>
static BOOLEAN AutomaticApproval;
#define main UnusedManualGateFixture
#include "test_local_approval.c"
#undef main

int main(void) {
  Passed = 0;
  for (unsigned quiet = 0; quiet < 2; quiet++) {
    for (unsigned input = 0; input < 8; input++) {
      SetupGate();
      Quiet = quiet;
      if (input == 1) SystemTable.ConIn = NULL;
      if (input == 2) Input.ReadKeyStroke = NULL;
      if (input == 3) ResetError = TRUE;
      if (input == 4) ReadError = TRUE;
      if (input == 5) StaleU = TRUE;
      if (input == 6) Typed = 'U';
      if (input == 7) Scan = 0x17;
      assert(Approve() == EFI_SUCCESS && AutomaticApproval);
      assert(!Reads && !Resets && !Waits && !LocalPrompt);
      Passed++;
    }
    for (unsigned key = 0; key < 4; key++) {
      SetupGate();
      Quiet = quiet;
      ProfileChanged = TRUE;
      AutomaticApproval = TRUE; /* A previous success must not leak into this call. */
      Typed = key == 0 ? 'U' : key == 1 ? 'u' : 0;
      Scan = key == 2 ? 0x17 : 0;
      EFI_STATUS expected = key < 2 ? EFI_SUCCESS : key == 2 ? EFI_ABORTED : EFI_TIMEOUT;
      assert(Approve() == expected && !AutomaticApproval && Resets == 1 && LocalPrompt);
      assert(key != 3 || (Reads == 300 && Waits == 300));
      Passed++;
    }
    SetupGate();
    Quiet = quiet;
    SayError = TRUE;
    AutomaticApproval = TRUE;
    assert(Approve() == EFI_DEVICE_ERROR && !AutomaticApproval && !Reads && !Resets);
    Passed++;
    SetupGate();
    Quiet = quiet;
    ProfileChanged = SayError = TRUE;
    assert(Approve() == EFI_DEVICE_ERROR && !AutomaticApproval && !Reads && !Resets);
    Passed++;
  }
  printf("PASS %u auto-approval cases: no keyboard on unchanged profile, debug/quiet, "
         "changed PROM retains U/Esc/timeout, console errors stop.\n", Passed);
  return 0;
}
