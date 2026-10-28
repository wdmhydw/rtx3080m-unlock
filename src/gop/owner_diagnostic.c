#include "owner_diagnostic.h"
#include "tree.h"
#include "windows.h"
#include "../unlock/ownership.h"

EFI_STATUS GdCollectOwnerDiagnostic(GC_CONTEXT *c) {
  GU_RELEASE_OWNER owner = {0};
  GW_PLAN windows = {0};
  if (!c->log("OWNER_DIAGNOSTIC_BEGIN: metadata only; no detach, claim, reset or StartImage\n"))
    return EFI_DEVICE_ERROR;
  EFI_STATUS tree = GcReadTree(c);
  if (!c->log("OWNER_TREE_RESULT status=%#llx\n", (unsigned long long)tree))
    return EFI_DEVICE_ERROR;
  EFI_STATUS candidate = GuFindReleaseOwner(c->table, &owner, TRUE);
  if (!c->log("OWNER_CANDIDATE_RESULT status=%#llx agent=%#llx image=%#llx bytes=%llu "
              "version=%#x metadata_only=1\n",
              (unsigned long long)candidate, (unsigned long long)(UINTN)owner.agent,
              (unsigned long long)(UINTN)owner.image, (unsigned long long)owner.size,
              owner.version))
    return EFI_DEVICE_ERROR;
  EFI_STATUS prepared = GwPrepare(c, &windows);
  BOOLEAN logged = c->log("OWNER_WINDOWS_PREFLIGHT status=%#llx image=%#llx start_requested=0\n",
                          (unsigned long long)prepared, (unsigned long long)(UINTN)windows.image);
  /* LoadImage may have partly succeeded even on an error. Always unload it. */
  EFI_STATUS cancelled = GwCancel(c, &windows);
  if (!c->log("OWNER_DIAGNOSTIC_END tree=%#llx candidate=%#llx windows=%#llx unload=%#llx "
              "remaining_image=%#llx detach_attempted=0\n",
              (unsigned long long)tree, (unsigned long long)candidate, (unsigned long long)prepared,
              (unsigned long long)cancelled, (unsigned long long)(UINTN)windows.image) ||
      !logged)
    return EFI_DEVICE_ERROR;
  if (EFI_ERROR(cancelled) || windows.image)
    return EFI_ERROR(cancelled) ? cancelled : EFI_DEVICE_ERROR;
  /* A completed report does not approve the owner or override the failed gate. */
  return EFI_ERROR(tree) ? tree : EFI_SUCCESS;
}
