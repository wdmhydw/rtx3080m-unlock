#include "single_owner.h"
#include "app_io.h"
#include <string.h>

/* Local experiment, restricted to this machine's observed board and driver.
   Metadata matching is a prerequisite, not a hardware compatibility claim. */
static BOOLEAN BoardMatches(VOID) {
  return GU_BOARD_ID == 0x24dc10deU && GU_BOARD_SUBSYSTEM == 0x3f8417aaU &&
         GU_BOARD_CHIP == 0xb74000a1U && GU_FB_MIB == 0x4000U;
}
static BOOLEAN DriverMatches(const GU_RELEASE_OWNER *owner) {
  UINTN base = (UINTN)owner->base, stop = (UINTN)owner->stop;
  return owner->version == 0x20 && owner->size == 7200 && base && stop >= base &&
         stop - base == 0xa64 && stop - base < owner->size;
}
EFI_STATUS GuSingleOwnerPrepare(EFI_SYSTEM_TABLE *table, GU_SINGLE_OWNER_PLAN *plan) {
  memset(plan, 0, sizeof(*plan));
  if (!table || table->BootServices != Gu.bs || !Gu.pci || !BoardMatches() || Gu.owned ||
      Gu.armed || Gu.started || Gu.fault || !GuLogReady())
    return EFI_ACCESS_DENIED;
  BOOLEAN idle = GuControllerIdle(FALSE);
  if (!GuLogReady()) return EFI_DEVICE_ERROR;
  if (!idle) {
    EFI_STATUS status = GuFindReleaseOwner(table, &plan->owner, TRUE);
    if (EFI_ERROR(status)) return status;
    if (!DriverMatches(&plan->owner)) return EFI_UNSUPPORTED;
    plan->release = TRUE;
  }
  plan->table = table;
  plan->target = Gu.handle;
  plan->image = Gu.image;
  plan->pci = Gu.pci;
  GuLog("SINGLE_OWNER_PLAN release=%u board=%08x subsystem=%08x agent=%#llx "
#ifdef GU_LENOVO_AUTOSTART
        "version=%#x bytes=%llu; configured approval and fresh checks required\n",
#else
        "version=%#x bytes=%llu; physical U and fresh checks required\n",
#endif
        plan->release, GU_BOARD_ID, GU_BOARD_SUBSYSTEM,
        (unsigned long long)(UINTN)plan->owner.agent, plan->owner.version,
        (unsigned long long)plan->owner.size);
  if (!GuLogReady()) return EFI_DEVICE_ERROR;
  plan->ready = TRUE;
  return EFI_SUCCESS;
}
EFI_STATUS GuSingleOwnerRelease(EFI_SYSTEM_TABLE *table, GU_SINGLE_OWNER_PLAN *plan,
                                BOOLEAN *touched, EFI_STATUS *detach) {
  *detach = EFI_NOT_STARTED;
  if (!plan->ready || plan->consumed || table != plan->table || table->BootServices != Gu.bs ||
      Gu.handle != plan->target || Gu.image != plan->image || Gu.pci != plan->pci ||
      !BoardMatches() || Gu.owned || Gu.armed || Gu.started || Gu.fault || !GuLogReady())
    return EFI_ACCESS_DENIED;
  plan->consumed = TRUE;
  if (plan->release) {
    GU_RELEASE_OWNER current;
    GuLog("SINGLE_OWNER_RELEASE_INTENT target=%#llx agent=%#llx; one DisconnectController; "
          "display remains on independent GPU\n",
          (unsigned long long)(UINTN)Gu.handle, (unsigned long long)(UINTN)plan->owner.agent);
    if (!GuLogReady()) return EFI_DEVICE_ERROR;
    /* No file or console access between the final owner snapshot and release. */
    EFI_STATUS status = GuFindReleaseOwner(table, &current, FALSE);
    if (EFI_ERROR(status)) return status;
    if (!DriverMatches(&current) || !GuSameReleaseOwner(&plan->owner, &current)) return EFI_ABORTED;
    EFI_PCI_IO_PROTOCOL *prior = Gu.pci;
    *touched = TRUE;
    Gu.armed = TRUE;
    Gu.pci = NULL;
    *detach = Gu.bs->DisconnectController(Gu.handle, current.agent, NULL);
    EFI_TPL old = Gu.bs->RaiseTPL(TPL_NOTIFY);
    EFI_STATUS refreshed = GuRefreshTargetPci();
    Gu.bs->RestoreTPL(old);
    GuLog("SINGLE_OWNER_RELEASE_RESULT detach=%#llx refresh=%#llx same_pci=%u\n",
          (unsigned long long)*detach, (unsigned long long)refreshed, Gu.pci == prior);
    if (EFI_ERROR(*detach)) return *detach;
    if (EFI_ERROR(refreshed)) return refreshed;
    if (Gu.pci != prior) return EFI_COMPROMISED_DATA;
    if (!GuLogReady()) return EFI_DEVICE_ERROR;
  }
  /* An unchanged or newly attached owner stops the experiment before claiming. */
  return GuControllerIdle(FALSE) ? EFI_SUCCESS : EFI_ACCESS_DENIED;
}
