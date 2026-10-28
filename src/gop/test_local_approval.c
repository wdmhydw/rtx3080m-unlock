#include <Uefi.h>
#include <assert.h>
#include <stdio.h>
#include <string.h>
static EFI_SYSTEM_TABLE SystemTable, *Table = &SystemTable;
static EFI_SIMPLE_TEXT_INPUT_PROTOCOL Input;
static EFI_BOOT_SERVICES Services;
static BOOLEAN Quiet, ProfileChanged, ResetError, ReadError, SayError, StaleU, LocalPrompt;
static unsigned Reads, Resets, Waits, Passed;
static CHAR16 Typed;
static UINT16 Scan;
static EFI_STATUS Say(CHAR16 *text) {
  const char needle[] = "LOCAL LENOVO EXPERIMENT";
  for (UINTN i = 0; text[i]; i++) {
    UINTN j = 0;
    while (needle[j] && text[i + j] == (UINT8)needle[j])
      j++;
    if (!needle[j]) LocalPrompt = TRUE;
  }
  return SayError ? EFI_DEVICE_ERROR : EFI_SUCCESS;
}
static EFI_STATUS EFIAPI Reset(EFI_SIMPLE_TEXT_INPUT_PROTOCOL *self, BOOLEAN extended) {
  (void)self;
  (void)extended;
  Resets++;
  StaleU = FALSE;
  return ResetError ? EFI_DEVICE_ERROR : EFI_SUCCESS;
}
static EFI_STATUS EFIAPI Read(EFI_SIMPLE_TEXT_INPUT_PROTOCOL *self, EFI_INPUT_KEY *key) {
  (void)self;
  Reads++;
  if (ReadError) return EFI_DEVICE_ERROR;
  if (Reads != 1 || (!Typed && !Scan && !StaleU)) return EFI_NOT_READY;
  key->UnicodeChar = StaleU ? 'U' : Typed;
  key->ScanCode = Scan;
  return EFI_SUCCESS;
}
static EFI_STATUS EFIAPI Wait(UINTN us) {
  assert(us == 100000);
  Waits++;
  return EFI_SUCCESS;
}
/* The actual production confirmation function, extracted without edits. */
#include "../../build/single-owner-check/approve-under-test.c"
static VOID SetupGate(VOID) {
  memset(&SystemTable, 0, sizeof(SystemTable));
  memset(&Input, 0, sizeof(Input));
  memset(&Services, 0, sizeof(Services));
  Input.Reset = Reset;
  Input.ReadKeyStroke = Read;
  Services.Stall = Wait;
  SystemTable.ConIn = &Input;
  SystemTable.BootServices = &Services;
  Quiet = ProfileChanged = ResetError = ReadError = SayError = StaleU = LocalPrompt = FALSE;
  Reads = Resets = Waits = 0;
  Typed = 0;
  Scan = 0;
}
int main(void) {
  for (unsigned quiet = 0; quiet < 2; quiet++)
    for (unsigned changed = 0; changed < 2; changed++)
      for (unsigned key = 0; key < 4; key++) {
        SetupGate();
        Quiet = quiet;
        ProfileChanged = changed;
        Typed = key == 0 ? 'U' : key == 1 ? 'u' : 0;
        Scan = key == 2 ? 0x17 : 0;
        EFI_STATUS expected = key < 2 ? EFI_SUCCESS : key == 2 ? EFI_ABORTED : EFI_TIMEOUT;
        assert(Approve() == expected && Resets == 1 && LocalPrompt);
        assert(key != 3 || (Reads == 300 && Waits == 300));
        Passed++;
      }
  SetupGate();
  StaleU = TRUE;
  assert(Approve() == EFI_TIMEOUT && Resets == 1);
  Passed++;
  SetupGate();
  SystemTable.ConIn = NULL;
  assert(Approve() == EFI_UNSUPPORTED && !Reads);
  Passed++;
  SetupGate();
  Input.ReadKeyStroke = NULL;
  assert(Approve() == EFI_UNSUPPORTED && !Reads);
  Passed++;
  SetupGate();
  ResetError = TRUE;
  Typed = 'U';
  assert(Approve() == EFI_DEVICE_ERROR && !Reads);
  Passed++;
  SetupGate();
  ReadError = TRUE;
  Typed = 'U';
  assert(Approve() == EFI_DEVICE_ERROR);
  Passed++;
  SetupGate();
  SayError = TRUE;
  Typed = 'U';
  assert(Approve() == EFI_DEVICE_ERROR && !Reads);
  Passed++;
  SetupGate();
  SayError = TRUE;
  ProfileChanged = TRUE;
  Typed = 'U';
  assert(Approve() == EFI_DEVICE_ERROR && !Reads && !Resets);
  Passed++;
  printf("PASS %u actual local U-gate cases, including quiet mode and stale key rejection.\n",
         Passed);
  return 0;
}
