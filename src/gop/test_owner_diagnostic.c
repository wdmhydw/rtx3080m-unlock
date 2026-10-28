#include "owner_diagnostic.h"
#include "tree.h"
#include "windows.h"
#include "../unlock/ownership.h"
#include <assert.h>
#include <stdio.h>
#include <string.h>

static GC_CONTEXT Context;
static EFI_SYSTEM_TABLE Table;
static EFI_BOOT_SERVICES Services;
static EFI_STATUS TreeResult, CandidateResult, WindowsResult, CancelResult;
static unsigned Logs, FailLog, Trees, Candidates, Prepares, Cancels;
static BOOLEAN LeaveImage;
static BOOLEAN Log(const char *format, ...) {
  (void)format;
  return ++Logs != FailLog;
}
EFI_STATUS GcReadTree(GC_CONTEXT *c) {
  assert(c == &Context);
  Trees++;
  return TreeResult;
}
EFI_STATUS GuFindReleaseOwner(EFI_SYSTEM_TABLE *table, GU_RELEASE_OWNER *owner, BOOLEAN log) {
  assert(table == &Table && log && !owner->agent);
  Candidates++;
  owner->agent = (EFI_HANDLE)(UINTN)2;
  owner->image = (EFI_HANDLE)(UINTN)3;
  owner->size = 65536;
  owner->version = 0x10;
  return CandidateResult;
}
EFI_STATUS GwPrepare(GC_CONTEXT *c, GW_PLAN *plan) {
  assert(c == &Context && !plan->image);
  Prepares++;
  plan->image = (EFI_HANDLE)(UINTN)4;
  return WindowsResult;
}
EFI_STATUS GwCancel(GC_CONTEXT *c, GW_PLAN *plan) {
  assert(c == &Context && plan->image);
  Cancels++;
  if (!LeaveImage) plan->image = NULL;
  return CancelResult;
}
static VOID Reset(VOID) {
  memset(&Context, 0, sizeof(Context));
  memset(&Table, 0, sizeof(Table));
  memset(&Services, 0, sizeof(Services));
  Context.table = &Table;
  Context.log = Log;
  Table.BootServices = &Services;
  TreeResult = CandidateResult = WindowsResult = CancelResult = EFI_SUCCESS;
  Logs = FailLog = Trees = Candidates = Prepares = Cancels = 0;
  LeaveImage = FALSE;
  /* No driver stop/start, exclusive acquisition, GPU access or StartImage exists. */
}
int main(void) {
  Reset();
  assert(GdCollectOwnerDiagnostic(&Context) == EFI_SUCCESS);
  assert(Trees == 1 && Candidates == 1 && Prepares == 1 && Cancels == 1);
  Reset();
  CandidateResult = EFI_ACCESS_DENIED;
  assert(GdCollectOwnerDiagnostic(&Context) == EFI_SUCCESS && Cancels == 1);
  Reset();
  WindowsResult = EFI_LOAD_ERROR;
  assert(GdCollectOwnerDiagnostic(&Context) == EFI_SUCCESS && Cancels == 1);
  Reset();
  TreeResult = EFI_COMPROMISED_DATA;
  assert(GdCollectOwnerDiagnostic(&Context) == EFI_COMPROMISED_DATA && Cancels == 1);
  Reset();
  CancelResult = EFI_DEVICE_ERROR;
  assert(GdCollectOwnerDiagnostic(&Context) == EFI_DEVICE_ERROR);
  Reset();
  LeaveImage = TRUE;
  assert(GdCollectOwnerDiagnostic(&Context) == EFI_DEVICE_ERROR);
  Reset();
  FailLog = 1;
  assert(GdCollectOwnerDiagnostic(&Context) == EFI_DEVICE_ERROR && !Trees && !Prepares);
  Reset();
  FailLog = 2;
  assert(GdCollectOwnerDiagnostic(&Context) == EFI_DEVICE_ERROR && Trees == 1 && !Candidates);
  Reset();
  FailLog = 3;
  assert(GdCollectOwnerDiagnostic(&Context) == EFI_DEVICE_ERROR && Candidates == 1 && !Prepares);
  Reset();
  FailLog = 4;
  assert(GdCollectOwnerDiagnostic(&Context) == EFI_DEVICE_ERROR && Cancels == 1);
  Reset();
  FailLog = 5;
  assert(GdCollectOwnerDiagnostic(&Context) == EFI_DEVICE_ERROR && Cancels == 1);
  puts("PASS 11 diagnostic collection and cleanup scenarios; hardware mutation unavailable");
  return 0;
}
