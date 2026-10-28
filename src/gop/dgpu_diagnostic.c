/* Read firmware-owned protocol metadata only. No GPU MMIO, PCI writes,
   GOP methods, driver bindings, controller connect/disconnect or GPU resets. */
#include "dgpu_diagnostic.h"
#include "tree.h"
#include "windows.h"
#include <Protocol/DevicePath.h>

static EFI_GUID GopGuid = EFI_GRAPHICS_OUTPUT_PROTOCOL_GUID,
                PathGuid = EFI_DEVICE_PATH_PROTOCOL_GUID, PciGuid = EFI_PCI_IO_PROTOCOL_GUID;

EFI_STATUS GdReadDisplays(GC_CONTEXT *c) {
  EFI_BOOT_SERVICES *bs = c->table->BootServices;
  EFI_HANDLE *handles = NULL;
  UINTN count = 0, target_paths = 0, other_paths = 0, unresolved = 0;
  EFI_STATUS status = bs->LocateHandleBuffer(ByProtocol, &GopGuid, NULL, &count, &handles);
  if (!c->log("DISPLAY_BEGIN status=%#llx count=%zu console_in=%#llx console_out=%#llx "
              "stderr=%#llx\n",
              (unsigned long long)status, (size_t)count,
              (unsigned long long)(UINTN)c->table->ConsoleInHandle,
              (unsigned long long)(UINTN)c->table->ConsoleOutHandle,
              (unsigned long long)(UINTN)c->table->StandardErrorHandle))
    status = EFI_DEVICE_ERROR;
  if (!EFI_ERROR(status) && (!handles || !count || count > 64)) status = EFI_COMPROMISED_DATA;
  for (UINTN i = 0; !EFI_ERROR(status) && i < count; i++) {
    EFI_GRAPHICS_OUTPUT_PROTOCOL *gop = NULL;
    EFI_DEVICE_PATH_PROTOCOL *path = NULL;
    EFI_HANDLE resolved = NULL;
    status = bs->HandleProtocol(handles[i], &GopGuid, (VOID **)&gop);
    if (EFI_ERROR(status) || !gop || !gop->Mode || !gop->Mode->Info ||
        gop->Mode->SizeOfInfo < sizeof(*gop->Mode->Info)) {
      if (!EFI_ERROR(status)) status = EFI_COMPROMISED_DATA;
      break;
    }
    EFI_STATUS path_status = bs->HandleProtocol(handles[i], &PathGuid, (VOID **)&path);
    EFI_STATUS resolved_status = EFI_NOT_FOUND;
    if (!EFI_ERROR(path_status) && path)
      resolved_status = bs->LocateDevicePath(&PciGuid, &path, &resolved);
    if (!EFI_ERROR(resolved_status) && resolved) {
      if (resolved == c->target)
        target_paths++;
      else
        other_paths++;
    } else
      unresolved++;
    EFI_GRAPHICS_OUTPUT_PROTOCOL_MODE *m = gop->Mode;
    EFI_GRAPHICS_OUTPUT_MODE_INFORMATION *n = m->Info;
    if (!c->log("DISPLAY_PATH handle=%#llx path_status=%#llx resolve_status=%#llx pci=%#llx "
                "matches_target=%u console_out=%u\n",
                (unsigned long long)(UINTN)handles[i], (unsigned long long)path_status,
                (unsigned long long)resolved_status, (unsigned long long)(UINTN)resolved,
                resolved == c->target, handles[i] == c->table->ConsoleOutHandle) ||
        !c->log("DISPLAY_MODE handle=%#llx mode=%u max=%u info_bytes=%zu version=%u "
                "width=%u height=%u stride=%u format=%u fb=%#llx fb_bytes=%zu\n",
                (unsigned long long)(UINTN)handles[i], m->Mode, m->MaxMode, (size_t)m->SizeOfInfo,
                n->Version, n->HorizontalResolution, n->VerticalResolution, n->PixelsPerScanLine,
                (unsigned)n->PixelFormat, (unsigned long long)m->FrameBufferBase,
                (size_t)m->FrameBufferSize) ||
        !c->log("DISPLAY_MASK handle=%#llx red=%08x green=%08x blue=%08x reserved=%08x\n",
                (unsigned long long)(UINTN)handles[i], n->PixelInformation.RedMask,
                n->PixelInformation.GreenMask, n->PixelInformation.BlueMask,
                n->PixelInformation.ReservedMask))
      status = EFI_DEVICE_ERROR;
  }
  if (handles && EFI_ERROR(bs->FreePool(handles))) status = EFI_DEVICE_ERROR;
  if (!c->log("DISPLAY_END status=%#llx target_paths=%zu other_paths=%zu unresolved=%zu "
              "gpu_writes=0\n",
              (unsigned long long)status, (size_t)target_paths, (size_t)other_paths,
              (size_t)unresolved))
    status = EFI_DEVICE_ERROR;
  return status;
}

EFI_STATUS GdCaptureAndBoot(GC_CONTEXT *c, EFI_FILE_PROTOCOL **report) {
  if (!report || !*report || c->quiet) return EFI_INVALID_PARAMETER;
  if (c->identity != 0x24dc10de || c->subsystem != 0x3f8417aa) return EFI_UNSUPPORTED;
  EFI_STATUS displays = GdReadDisplays(c);
  /* Keep the ownership tree even when a firmware GOP mode is incomplete. */
  EFI_STATUS tree = GcReadTree(c);
  if (!c->log("DGPU_DIAGNOSTIC_RESULT displays=%#llx tree=%#llx touched=0 "
              "unlock=0 flr=0\n",
              (unsigned long long)displays, (unsigned long long)tree))
    return EFI_DEVICE_ERROR;
  EFI_STATUS status = EFI_ERROR(displays) ? displays : tree;
  if (EFI_ERROR(status)) return status;
  GW_PLAN windows = {0};
  status = GwPrepare(c, &windows);
  if (!EFI_ERROR(status)) {
    if (!c->log("DGPU_DIAGNOSTIC_COMPLETE: firmware metadata saved; starting original Windows\n"))
      status = EFI_DEVICE_ERROR;
    else
      status = GwStart(c, &windows, report, TRUE);
  }
  /* StartImage should not return after successful OS boot. */
  EFI_STATUS cleanup = GwCancel(c, &windows);
  return EFI_ERROR(cleanup) ? cleanup : status;
}
