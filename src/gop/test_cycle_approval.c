#include <Uefi.h>
#include <assert.h>
#include <stdio.h>
#include <string.h>
static EFI_SYSTEM_TABLE SystemTable, *Table = &SystemTable;
static EFI_BOOT_SERVICES Bs;
static EFI_SIMPLE_TEXT_INPUT_PROTOCOL Input;
static unsigned Reads, Resets, Waits, Cases;
static CHAR16 Typed;
static UINT16 Scan;
static BOOLEAN ResetError, ReadError, SayError, Stale;
static EFI_STATUS Say(CHAR16 *text) {
  assert(Table == &SystemTable);
  (void)text;
  return SayError ? EFI_DEVICE_ERROR : EFI_SUCCESS;
}
static EFI_STATUS EFIAPI Reset(EFI_SIMPLE_TEXT_INPUT_PROTOCOL *p, BOOLEAN v) {
  (void)p;
  (void)v;
  Resets++;
  Stale = FALSE;
  return ResetError ? EFI_DEVICE_ERROR : EFI_SUCCESS;
}
static EFI_STATUS EFIAPI Read(EFI_SIMPLE_TEXT_INPUT_PROTOCOL *p, EFI_INPUT_KEY *key) {
  (void)p;
  Reads++;
  if (ReadError) return EFI_DEVICE_ERROR;
  if (Reads != 1 || (!Typed && !Scan && !Stale)) return EFI_NOT_READY;
  key->UnicodeChar = Stale ? 'U' : Typed;
  key->ScanCode = Scan;
  return EFI_SUCCESS;
}
static EFI_STATUS EFIAPI Wait(UINTN us) {
  assert(us == 100000);
  Waits++;
  return EFI_SUCCESS;
}
#include "../../build/lenovo-cycle-check/approve-under-test.c"
static VOID Setup(VOID) {
  memset(&SystemTable, 0, sizeof(SystemTable));
  memset(&Bs, 0, sizeof(Bs));
  memset(&Input, 0, sizeof(Input));
  Input.ReadKeyStroke = Read;
  Input.Reset = Reset;
  Bs.Stall = Wait;
  SystemTable.BootServices = &Bs;
  SystemTable.ConIn = &Input;
  Reads = Resets = Waits = 0;
  Typed = Scan = 0;
  ResetError = ReadError = SayError = Stale = FALSE;
}
#define CHECK(x)                                                                                   \
  do {                                                                                             \
    assert(x);                                                                                     \
    Cases++;                                                                                       \
  } while (0)
int main(void) {
#ifdef GC_LENOVO_AUTOSTART
  Setup();
  CHECK(Approve() == EFI_SUCCESS && !Reads && !Resets && !Waits);
  Setup();
  SystemTable.ConIn = NULL;
  CHECK(Approve() == EFI_SUCCESS && !Reads && !Resets && !Waits);
  Setup();
  Input.ReadKeyStroke = NULL;
  Input.Reset = NULL;
  CHECK(Approve() == EFI_SUCCESS && !Reads && !Resets);
  Setup();
  Stale = TRUE;
  Scan = 0x17;
  CHECK(Approve() == EFI_SUCCESS && !Reads && !Waits);
  Setup();
  SayError = TRUE;
  CHECK(Approve() == EFI_DEVICE_ERROR && !Reads && !Waits);
  printf("PASS %u automatic start cases; no keyboard reads, resets or delays\n", Cases);
  return 0;
#endif
  Setup();
  Typed = 'U';
  CHECK(Approve() == EFI_SUCCESS && Resets == 1 && Reads == 1);
  Setup();
  Typed = 'u';
  CHECK(Approve() == EFI_SUCCESS && Resets == 1);
  Setup();
  Scan = 0x17;
  CHECK(Approve() == EFI_ABORTED);
  Setup();
  CHECK(Approve() == EFI_TIMEOUT && Waits == 300);
  Setup();
  Stale = TRUE;
  CHECK(Approve() == EFI_TIMEOUT && Resets == 1);
  Setup();
  SystemTable.ConIn = NULL;
  CHECK(Approve() == EFI_UNSUPPORTED && !Reads);
  Setup();
  Input.Reset = NULL;
  CHECK(Approve() == EFI_UNSUPPORTED && !Reads);
  Setup();
  Input.ReadKeyStroke = NULL;
  CHECK(Approve() == EFI_UNSUPPORTED && !Resets);
  Setup();
  ResetError = TRUE;
  CHECK(Approve() == EFI_DEVICE_ERROR && !Reads);
  Setup();
  ReadError = TRUE;
  CHECK(Approve() == EFI_DEVICE_ERROR);
  Setup();
  SayError = TRUE;
  CHECK(Approve() == EFI_DEVICE_ERROR && !Reads);
  printf("PASS %u actual cycle approval cases\n", Cases);
  return 0;
}
