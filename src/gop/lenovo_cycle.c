/* Lenovo 82K6 / H1CN58WW, qualified from dGPU diagnostic RUN00.
   GC_LENOVO_UNLOCK adds the SS0-only transaction inside the exclusive interval. */
#include "lenovo_cycle.h"
#ifdef GC_LENOVO_UNLOCK
#if !defined(GC_LENOVO_VIRTUAL_RELEASE) || !defined(GC_FULL_UNLOCK) ||                             \
    !defined(GU_INDEPENDENT_DISPLAY)
#error Lenovo unlock requires virtual withdrawal and the SS0-only transaction.
#endif
#include "patch.h"
#endif
#include <Protocol/DevicePath.h>
#include <Protocol/LoadedImage.h>
#include <Protocol/SimpleTextIn.h>
#include <Protocol/SimpleTextOut.h>
#include <string.h>

static EFI_GUID PciGuid = EFI_PCI_IO_PROTOCOL_GUID, PathGuid = EFI_DEVICE_PATH_PROTOCOL_GUID,
                GopGuid = EFI_GRAPHICS_OUTPUT_PROTOCOL_GUID,
                TextGuid = EFI_SIMPLE_TEXT_OUTPUT_PROTOCOL_GUID,
                InputGuid = EFI_SIMPLE_TEXT_INPUT_PROTOCOL_GUID,
                BindingGuid = EFI_DRIVER_BINDING_PROTOCOL_GUID,
                LoadedGuid = EFI_LOADED_IMAGE_PROTOCOL_GUID,
                ConsoleDeviceGuid = {
                    0xd3b36f2c, 0xd551, 0x11d4, {0x9a, 0x46, 0x00, 0x90, 0x27, 0x3f, 0xc1, 0x4d}};
static BOOLEAN Eq(const EFI_GUID *a, const EFI_GUID *b) { return !memcmp(a, b, sizeof(*a)); }
static EFI_STATUS Free(GC_CONTEXT *c, VOID *p, EFI_STATUS s) {
  return p && EFI_ERROR(c->table->BootServices->FreePool(p)) ? EFI_DEVICE_ERROR : s;
}
static BOOLEAN Missing(EFI_STATUS s) {
  return s == EFI_UNSUPPORTED || s == EFI_NOT_FOUND || s == EFI_INVALID_PARAMETER;
}
/* No logging or console calls: also used under TPL_NOTIFY before exclusive acquisition. */
static EFI_STATUS Records(GC_CONTEXT *c, EFI_HANDLE h, LC_SET *set) {
  EFI_BOOT_SERVICES *bs = c->table->BootServices;
  EFI_GUID **protocols = NULL;
  UINTN count = 0;
  EFI_STATUS s = bs->ProtocolsPerHandle(h, &protocols, &count);
  if (s == EFI_SUCCESS && (!protocols || !count || count > 64)) s = EFI_COMPROMISED_DATA;
  for (UINTN i = 0; s == EFI_SUCCESS && i < count; i++) {
    if (!protocols[i]) {
      s = EFI_COMPROMISED_DATA;
      break;
    }
    EFI_OPEN_PROTOCOL_INFORMATION_ENTRY *entries = NULL;
    UINTN n = 0;
    s = bs->OpenProtocolInformation(h, protocols[i], &entries, &n);
    if (s == EFI_SUCCESS && (n > 128 || (n && !entries))) s = EFI_COMPROMISED_DATA;
    for (UINTN j = 0; s == EFI_SUCCESS && j < n; j++) {
      const EFI_OPEN_PROTOCOL_INFORMATION_ENTRY *e = &entries[j];
      if (e->Attributes == 1 || e->Attributes == 2 || e->Attributes == 4) continue;
      if (set->count == LC_RECORDS) {
        s = EFI_BUFFER_TOO_SMALL;
        break;
      }
      LC_RECORD *r = &set->records[set->count++];
      memset(r, 0, sizeof(*r));
      r->handle = h;
      r->protocol = *protocols[i];
      r->agent = e->AgentHandle;
      r->controller = e->ControllerHandle;
      r->attributes = e->Attributes;
      r->count = e->OpenCount;
    }
    s = Free(c, entries, s);
  }
  s = Free(c, protocols, s);
  return s == EFI_SUCCESS || EFI_ERROR(s) ? s : EFI_DEVICE_ERROR;
}
static BOOLEAN SameSet(const LC_SET *a, const LC_SET *b) {
  if (a->count != b->count) return FALSE;
  for (UINTN i = 0; i < a->count; i++) {
    UINTN matches = 0;
    for (UINTN j = 0; j < b->count; j++)
      if (!memcmp(&a->records[i], &b->records[j], sizeof(LC_RECORD))) matches++;
    if (matches != 1) return FALSE;
  }
  return TRUE;
}
static EFI_STATUS Shared(GC_CONTEXT *c, const LC_PLAN *p, LC_SET *set) {
  EFI_SYSTEM_TABLE *t = c->table;
  EFI_HANDLE handles[] = {t->ConsoleInHandle, t->ConsoleOutHandle, t->StandardErrorHandle};
  VOID *interfaces[] = {t->ConIn, t->ConOut, t->StdErr};
  EFI_GUID *guids[] = {&InputGuid, &TextGuid, &TextGuid};
  if (memcmp(handles, p->consoles, sizeof(handles)) ||
      memcmp(interfaces, p->interfaces, sizeof(interfaces)))
    return EFI_COMPROMISED_DATA;
  memset(set, 0, sizeof(*set));
  for (UINTN i = 0; i < 3; i++) {
    if (!handles[i] || !interfaces[i] || handles[i] == c->target || handles[i] == p->child)
      return EFI_UNSUPPORTED;
    VOID *actual = NULL;
    EFI_STATUS s = t->BootServices->HandleProtocol(handles[i], guids[i], &actual);
    if (s != EFI_SUCCESS || actual != interfaces[i]) return EFI_COMPROMISED_DATA;
    BOOLEAN seen = FALSE;
    for (UINTN j = 0; j < i; j++)
      if (handles[i] == handles[j]) seen = TRUE;
    if (!seen) {
      s = Records(c, handles[i], set);
      if (EFI_ERROR(s)) return s;
    }
  }
  VOID *pci = NULL;
  EFI_STATUS s = t->BootServices->HandleProtocol(c->target, &PciGuid, &pci);
  return s == EFI_SUCCESS && pci == c->pci ? EFI_SUCCESS : EFI_COMPROMISED_DATA;
}
static EFI_STATUS SharedUnchanged(GC_CONTEXT *c, const LC_PLAN *p) {
  LC_SET set;
  EFI_STATUS s = Shared(c, p, &set);
  return !EFI_ERROR(s) && !SameSet(&set, &p->shared) ? EFI_COMPROMISED_DATA : s;
}
static EFI_STATUS Binding(GC_CONTEXT *c, EFI_HANDLE h, UINTN role, LC_DRIVER *d,
                          EFI_DRIVER_BINDING_PROTOCOL **out) {
  static const UINT32 versions[] = {0x6000d, 0xa, 0xa, 0xa};
  static const UINT64 bytes[] = {193248, 24608, 22016, 35840};
  static const UINTN stop_offsets[] = {0x698, 0x1444, 0xf34, 0x1a90};
  static const UINTN start_offsets[] = {0, 0x10c8, 0xcac, 0x153c};
  if (role > 3 || !h || h == c->image || h == c->target) return EFI_UNSUPPORTED;
  EFI_BOOT_SERVICES *bs = c->table->BootServices;
  EFI_LOADED_IMAGE_PROTOCOL *image = NULL;
  *out = NULL;
  EFI_STATUS s = bs->HandleProtocol(h, &BindingGuid, (VOID **)out);
  if (s != EFI_SUCCESS || !*out || (*out)->DriverBindingHandle != h || !(*out)->ImageHandle ||
      (*out)->ImageHandle == c->image || !(*out)->Supported || !(*out)->Start || !(*out)->Stop)
    return EFI_UNSUPPORTED;
  s = bs->HandleProtocol((*out)->ImageHandle, &LoadedGuid, (VOID **)&image);
  if (s != EFI_SUCCESS || !image || !image->ImageBase ||
      image->ImageCodeType != EfiBootServicesCode || image->ImageSize != bytes[role] ||
      (*out)->Version != versions[role])
    return EFI_UNSUPPORTED;
  UINTN base = (UINTN)image->ImageBase, stop = (UINTN)(*out)->Stop, start = (UINTN)(*out)->Start;
  if (stop < base || stop - base != stop_offsets[role] ||
      (role && (start < base || start - base != start_offsets[role])))
    return EFI_UNSUPPORTED;
  /* The observed NVIDIA Start is a firmware wrapper outside its image. Keep
     the exact pointer in the plan and revalidate all binding fields at every call. */
  memset(d, 0, sizeof(*d));
  d->driver = (GC_DRIVER){.handle = h,
                          .image = (*out)->ImageHandle,
                          .base = image->ImageBase,
                          .bytes = image->ImageSize,
                          .start = (*out)->Start,
                          .stop = (*out)->Stop,
                          .version = (*out)->Version};
  d->supported = (*out)->Supported;
  return EFI_SUCCESS;
}
static EFI_STATUS DriverNow(GC_CONTEXT *c, const LC_PLAN *p, UINTN role,
                            EFI_DRIVER_BINDING_PROTOCOL **out) {
  LC_DRIVER now;
  EFI_STATUS s = Binding(c, p->drivers[role].driver.handle, role, &now, out);
  return !EFI_ERROR(s) && memcmp(&now, &p->drivers[role], sizeof(now)) ? EFI_ABORTED : s;
}
static EFI_STATUS Root(GC_CONTEXT *c, EFI_HANDLE *owner, EFI_HANDLE *child) {
  LC_SET set = {0};
  *owner = *child = NULL;
  EFI_HANDLE agent = NULL;
  EFI_STATUS s = Records(c, c->target, &set);
  if (EFI_ERROR(s)) return s;
  if (set.count != 2) return EFI_ACCESS_DENIED;
  for (UINTN i = 0; i < set.count; i++) {
    const LC_RECORD *r = &set.records[i];
    if (r->count != 1 || !r->agent) return EFI_ACCESS_DENIED;
    if (Eq(&r->protocol, &PciGuid) && r->attributes == EFI_OPEN_PROTOCOL_BY_DRIVER &&
        r->controller == c->target && !*owner)
      *owner = r->agent;
    else if (Eq(&r->protocol, &PathGuid) &&
             r->attributes == EFI_OPEN_PROTOCOL_BY_CHILD_CONTROLLER && r->controller &&
             r->controller != c->target && !*child) {
      *child = r->controller;
      agent = r->agent;
    } else
      return EFI_ACCESS_DENIED;
  }
  return *owner && *child && *owner == agent ? EFI_SUCCESS : EFI_ACCESS_DENIED;
}
/* Bits: vendor owner, vendor child, graphics owner, platform owner,
   splitter owner, splitter-to-shared-console edge. Every active record is matched. */
static EFI_STATUS MatchShape(GC_CONTEXT *c, const LC_PLAN *p, EFI_HANDLE child, const LC_SET *set,
                             UINT32 *found) {
  *found = 0;
  for (UINTN i = 0; i < set->count; i++) {
    const LC_RECORD *r = &set->records[i];
    UINT32 bit = 0;
    if (r->count != 1) return EFI_ACCESS_DENIED;
    if (r->handle == c->target && r->agent == p->drivers[0].driver.handle) {
      if (Eq(&r->protocol, &PciGuid) && r->attributes == EFI_OPEN_PROTOCOL_BY_DRIVER &&
          r->controller == c->target)
        bit = 1;
      if (Eq(&r->protocol, &PathGuid) && r->attributes == EFI_OPEN_PROTOCOL_BY_CHILD_CONTROLLER &&
          r->controller == child)
        bit = 2;
    } else if (r->handle == child) {
      if (Eq(&r->protocol, &GopGuid) && r->agent == p->drivers[1].driver.handle &&
          r->attributes == EFI_OPEN_PROTOCOL_BY_DRIVER && r->controller == child)
        bit = 4;
      if (Eq(&r->protocol, &TextGuid) && r->agent == p->drivers[2].driver.handle &&
          r->attributes == EFI_OPEN_PROTOCOL_BY_DRIVER && r->controller == child)
        bit = 8;
      if (Eq(&r->protocol, &ConsoleDeviceGuid) && r->agent == p->drivers[3].driver.handle) {
        if (r->attributes == EFI_OPEN_PROTOCOL_BY_DRIVER && r->controller == child) bit = 16;
        if (r->attributes == EFI_OPEN_PROTOCOL_BY_CHILD_CONTROLLER &&
            r->controller == p->consoles[1])
          bit = 32;
      }
    }
    if (!bit || (*found & bit)) return EFI_ACCESS_DENIED;
    *found |= bit;
  }
  return EFI_SUCCESS;
}
static EFI_STATUS Shape(GC_CONTEXT *c, const LC_PLAN *p, EFI_HANDLE child, UINT32 expected) {
  LC_SET set = {0};
  EFI_STATUS s = Records(c, c->target, &set);
  if (!EFI_ERROR(s) && (expected & 2)) s = Records(c, child, &set);
  UINT32 found = 0;
  if (!EFI_ERROR(s)) s = MatchShape(c, p, child, &set, &found);
  if (!EFI_ERROR(s)) s = found == expected ? SharedUnchanged(c, p) : EFI_ACCESS_DENIED;
  return s;
}
static BOOLEAN ValidMode(EFI_GRAPHICS_OUTPUT_PROTOCOL_MODE *m) {
  if (!m || !m->Info || m->SizeOfInfo < sizeof(*m->Info) || !m->MaxMode || m->Mode >= m->MaxMode ||
      !m->FrameBufferBase || !m->FrameBufferSize ||
      m->FrameBufferBase > MAX_UINT64 - m->FrameBufferSize)
    return FALSE;
  EFI_GRAPHICS_OUTPUT_MODE_INFORMATION *i = m->Info;
  return i->PixelFormat <= PixelBlueGreenRedReserved8BitPerColor && i->HorizontalResolution &&
         i->VerticalResolution && i->PixelsPerScanLine >= i->HorizontalResolution &&
         (UINT64)i->PixelsPerScanLine * i->VerticalResolution <= m->FrameBufferSize / 4;
}
static BOOLEAN SameMode(const EFI_GRAPHICS_OUTPUT_PROTOCOL_MODE *a,
                        const EFI_GRAPHICS_OUTPUT_PROTOCOL_MODE *b) {
  return a->Mode == b->Mode && a->MaxMode == b->MaxMode && a->SizeOfInfo == b->SizeOfInfo &&
         a->FrameBufferBase == b->FrameBufferBase && a->FrameBufferSize == b->FrameBufferSize &&
         !memcmp(a->Info, b->Info, sizeof(*a->Info));
}
#ifdef GC_LENOVO_VIRTUAL_RELEASE
static BOOLEAN VirtualMethodsInDriver(const LC_PLAN *p, EFI_GRAPHICS_OUTPUT_PROTOCOL *gop) {
  UINTN methods[] = {(UINTN)gop->QueryMode, (UINTN)gop->SetMode, (UINTN)gop->Blt};
  UINTN base = (UINTN)p->drivers[3].driver.base;
  for (UINTN i = 0; i < 3; i++)
    if (!methods[i] || methods[i] < base || methods[i] - base >= p->drivers[3].driver.bytes)
      return FALSE;
  return TRUE;
}
static BOOLEAN VirtualMemorySame(const LC_PLAN *p) {
  EFI_GRAPHICS_OUTPUT_PROTOCOL *gop = p->virtual_gop;
  return gop && !memcmp(gop, &p->virtual_interface, sizeof(*gop)) && gop->Mode &&
         !memcmp(gop->Mode, &p->virtual_mode, sizeof(*gop->Mode)) && gop->Mode->Info &&
         !memcmp(gop->Mode->Info, &p->info, sizeof(p->info));
}
#endif
static EFI_STATUS Displays(GC_CONTEXT *c, LC_PLAN *p) {
  EFI_BOOT_SERVICES *bs = c->table->BootServices;
  EFI_HANDLE *handles = NULL;
  UINTN count = 0;
  EFI_STATUS s = bs->LocateHandleBuffer(ByProtocol, &GopGuid, NULL, &count, &handles);
  EFI_GRAPHICS_OUTPUT_PROTOCOL *physical = NULL, *mirror = NULL;
  if (s == EFI_SUCCESS && (!handles || count != 2)) s = EFI_UNSUPPORTED;
  for (UINTN i = 0; s == EFI_SUCCESS && i < count; i++) {
    EFI_GRAPHICS_OUTPUT_PROTOCOL *gop = NULL;
    s = bs->HandleProtocol(handles[i], &GopGuid, (VOID **)&gop);
    if (s != EFI_SUCCESS || !gop || !ValidMode(gop->Mode)) {
      s = EFI_UNSUPPORTED;
      break;
    }
    if (!c->log("LC_DISPLAY handle=%#llx physical=%u console=%u mode=%u max=%u "
                "width=%u height=%u stride=%u format=%u framebuffer=%#llx bytes=%zu\n",
                (unsigned long long)(UINTN)handles[i], handles[i] == p->child,
                handles[i] == p->consoles[1], gop->Mode->Mode, gop->Mode->MaxMode,
                gop->Mode->Info->HorizontalResolution, gop->Mode->Info->VerticalResolution,
                gop->Mode->Info->PixelsPerScanLine, (unsigned)gop->Mode->Info->PixelFormat,
                (unsigned long long)gop->Mode->FrameBufferBase,
                (size_t)gop->Mode->FrameBufferSize)) {
      s = EFI_DEVICE_ERROR;
      break;
    }
    EFI_DEVICE_PATH_PROTOCOL *path = NULL;
    EFI_STATUS ps = bs->HandleProtocol(handles[i], &PathGuid, (VOID **)&path);
    if (handles[i] == p->child && !physical) {
      EFI_HANDLE resolved = NULL;
      if (ps != EFI_SUCCESS || !path ||
          bs->LocateDevicePath(&PciGuid, &path, &resolved) != EFI_SUCCESS ||
          resolved != c->target) {
        s = EFI_ACCESS_DENIED;
        break;
      }
      physical = gop;
    } else if (handles[i] == p->consoles[1] && !mirror && !path &&
               (ps == EFI_UNSUPPORTED || ps == EFI_NOT_FOUND))
      mirror = gop;
    else {
      s = EFI_ACCESS_DENIED;
      break;
    }
  }
  if (s == EFI_SUCCESS && (!physical || !mirror || !SameMode(physical->Mode, mirror->Mode)))
    s = EFI_UNSUPPORTED;
  if (s == EFI_SUCCESS) {
    p->gop = physical;
    p->info = *physical->Mode->Info;
    p->framebuffer = physical->Mode->FrameBufferBase;
    p->framebuffer_size = physical->Mode->FrameBufferSize;
    p->mode = physical->Mode->Mode;
    p->max_mode = physical->Mode->MaxMode;
    p->info_size = physical->Mode->SizeOfInfo;
#ifdef GC_LENOVO_VIRTUAL_RELEASE
    BOOLEAN methods = VirtualMethodsInDriver(p, mirror);
    if (!c->log("LC_VIRTUAL_BASELINE interface=%#llx mode=%#llx query=%#llx set=%#llx blt=%#llx "
                "splitter_base=%#llx splitter_bytes=%llu methods_in_driver=%u\n",
                (unsigned long long)(UINTN)mirror, (unsigned long long)(UINTN)mirror->Mode,
                (unsigned long long)(UINTN)mirror->QueryMode,
                (unsigned long long)(UINTN)mirror->SetMode, (unsigned long long)(UINTN)mirror->Blt,
                (unsigned long long)(UINTN)p->drivers[3].driver.base,
                (unsigned long long)p->drivers[3].driver.bytes, methods))
      s = EFI_DEVICE_ERROR;
    else if (!methods || !bs->UninstallProtocolInterface || !bs->InstallProtocolInterface)
      s = EFI_UNSUPPORTED;
    else {
      p->virtual_gop = mirror;
      p->virtual_interface = *mirror;
      p->virtual_mode = *mirror->Mode;
    }
#endif
  }
  s = Free(c, handles, s);
  return s == EFI_SUCCESS || EFI_ERROR(s) ? s : EFI_DEVICE_ERROR;
}
EFI_STATUS LcPrepare(GC_CONTEXT *c, LC_PLAN *p) {
  memset(p, 0, sizeof(*p));
  if (c->identity != 0x24dc10de || c->subsystem != 0x3f8417aa || c->class_revision != 0x030000a1 ||
      c->location[0] || c->location[1] != 1 || c->location[2] || c->location[3])
    return EFI_UNSUPPORTED;
  EFI_HANDLE agents[4] = {0};
  EFI_STATUS s = Root(c, &agents[0], &p->child);
  if (EFI_ERROR(s)) return s;
  p->consoles[0] = c->table->ConsoleInHandle;
  p->consoles[1] = c->table->ConsoleOutHandle;
  p->consoles[2] = c->table->StandardErrorHandle;
  p->interfaces[0] = c->table->ConIn;
  p->interfaces[1] = c->table->ConOut;
  p->interfaces[2] = c->table->StdErr;
  s = Shared(c, p, &p->shared);
  if (EFI_ERROR(s)) return s;
  LC_SET set = {0};
  s = Records(c, p->child, &set);
  if (EFI_ERROR(s)) return s;
  for (UINTN i = 0; i < set.count; i++) {
    LC_RECORD *r = &set.records[i];
    if (r->attributes != EFI_OPEN_PROTOCOL_BY_DRIVER) continue;
    UINTN role = Eq(&r->protocol, &GopGuid)             ? 1
                 : Eq(&r->protocol, &TextGuid)          ? 2
                 : Eq(&r->protocol, &ConsoleDeviceGuid) ? 3
                                                        : 4;
    if (role > 3 || agents[role]) return EFI_ACCESS_DENIED;
    agents[role] = r->agent;
  }
  for (UINTN i = 0; i < 4; i++) {
    for (UINTN j = 0; j < i; j++)
      if (agents[i] == agents[j]) return EFI_ACCESS_DENIED;
    EFI_DRIVER_BINDING_PROTOCOL *binding = NULL;
    s = Binding(c, agents[i], i, &p->drivers[i], &binding);
    if (EFI_ERROR(s)) return s;
    GC_DRIVER *d = &p->drivers[i].driver;
    if (!c->log("LC_DRIVER role=%zu agent=%#llx image=%#llx base=%#llx bytes=%llu version=%#x "
                "start=%#llx stop=%#llx supported=%#llx\n",
                (size_t)i, (unsigned long long)(UINTN)d->handle,
                (unsigned long long)(UINTN)d->image, (unsigned long long)(UINTN)d->base,
                (unsigned long long)d->bytes, d->version, (unsigned long long)(UINTN)d->start,
                (unsigned long long)(UINTN)d->stop,
                (unsigned long long)(UINTN)p->drivers[i].supported))
      return EFI_DEVICE_ERROR;
  }
  s = Shape(c, p, p->child, 63);
  if (!EFI_ERROR(s)) s = Displays(c, p);
  if (!EFI_ERROR(s)) s = GfSavePci(c, &p->pci);
  if (!c->log("LC_PREPARE status=%#llx child=%#llx shared=%#llx\n", (unsigned long long)s,
              (unsigned long long)(UINTN)p->child, (unsigned long long)(UINTN)p->consoles[1]))
    return EFI_DEVICE_ERROR;
  return s;
}
static BOOLEAN SamePlan(const LC_PLAN *a, const LC_PLAN *b, BOOLEAN before) {
  return (!before || (a->child == b->child && a->gop == b->gop)) &&
#ifdef GC_LENOVO_VIRTUAL_RELEASE
         a->virtual_gop == b->virtual_gop &&
         !memcmp(&a->virtual_interface, &b->virtual_interface, sizeof(a->virtual_interface)) &&
         !memcmp(&a->virtual_mode, &b->virtual_mode, sizeof(a->virtual_mode)) &&
#endif
         !memcmp(a->drivers, b->drivers, sizeof(a->drivers)) &&
         !memcmp(a->consoles, b->consoles, sizeof(a->consoles)) &&
         !memcmp(a->interfaces, b->interfaces, sizeof(a->interfaces)) &&
         SameSet(&a->shared, &b->shared) && !memcmp(&a->info, &b->info, sizeof(a->info)) &&
         a->mode == b->mode && a->max_mode == b->max_mode && a->framebuffer == b->framebuffer &&
         a->framebuffer_size == b->framebuffer_size && a->info_size == b->info_size &&
         !memcmp(&a->pci, &b->pci, sizeof(a->pci));
}
static EFI_STATUS Stop(GC_CONTEXT *c, const LC_PLAN *p, UINTN role, EFI_HANDLE parent, UINTN n,
                       EFI_HANDLE *children, UINT32 before, UINT32 after, GC_RESULT *r) {
  if (!c->log("LC_STOP_INTENT role=%zu parent=%#llx children=%zu\n", (size_t)role,
              (unsigned long long)(UINTN)parent, (size_t)n))
    return EFI_DEVICE_ERROR;
  EFI_DRIVER_BINDING_PROTOCOL *binding = NULL;
  EFI_STATUS s = Shape(c, p, p->child, before);
  if (!EFI_ERROR(s)) s = DriverNow(c, p, role, &binding);
  if (EFI_ERROR(s)) return s;
  r->touched = TRUE;
  s = binding->Stop(binding, parent, n, children);
  EFI_STATUS shape = Shape(c, p, p->child, after);
  if (!c->log("LC_STOP_RESULT role=%zu status=%#llx shape=%#llx\n", (size_t)role,
              (unsigned long long)s, (unsigned long long)shape))
    return EFI_DEVICE_ERROR;
  return s != EFI_SUCCESS ? (EFI_ERROR(s) ? s : EFI_DEVICE_ERROR) : shape;
}
static EFI_STATUS Start(GC_CONTEXT *c, const LC_PLAN *p, UINTN role, EFI_HANDLE parent,
                        EFI_HANDLE child, UINT32 before) {
  if (!c->log("LC_START_INTENT role=%zu parent=%#llx\n", (size_t)role,
              (unsigned long long)(UINTN)parent))
    return EFI_DEVICE_ERROR;
  EFI_DRIVER_BINDING_PROTOCOL *binding = NULL;
  EFI_STATUS s = Shape(c, p, child, before);
  EFI_STATUS driver = EFI_NOT_STARTED;
  if (!EFI_ERROR(s)) driver = DriverNow(c, p, role, &binding);
  if (!c->log("LC_START_CHECK role=%zu shape=%#llx driver=%#llx\n", (size_t)role,
              (unsigned long long)s, (unsigned long long)driver))
    return EFI_DEVICE_ERROR;
  if (!EFI_ERROR(s)) s = driver;
  if (EFI_ERROR(s)) return s;
  s = binding->Supported(binding, parent, NULL);
  if (!c->log("LC_SUPPORTED_RESULT role=%zu status=%#llx\n", (size_t)role, (unsigned long long)s))
    return EFI_DEVICE_ERROR;
  if (s == EFI_SUCCESS) s = binding->Start(binding, parent, NULL);
  if (!c->log("LC_START_RESULT role=%zu status=%#llx\n", (size_t)role, (unsigned long long)s))
    return EFI_DEVICE_ERROR;
  return s == EFI_SUCCESS || EFI_ERROR(s) ? s : EFI_DEVICE_ERROR;
}
static EFI_STATUS LogRecords(GC_CONTEXT *c, const char *scope, const LC_SET *set) {
  for (UINTN i = 0; i < set->count; i++) {
    const LC_RECORD *r = &set->records[i];
    const EFI_GUID *g = &r->protocol;
    if (!c->log("LC_RECOVERY_RECORD scope=%s handle=%#llx agent=%#llx controller=%#llx "
                "protocol=%08x-%04x-%04x-%02x%02x-%02x%02x%02x%02x%02x%02x "
                "attributes=%#x count=%u\n",
                scope, (unsigned long long)(UINTN)r->handle, (unsigned long long)(UINTN)r->agent,
                (unsigned long long)(UINTN)r->controller, g->Data1, g->Data2, g->Data3, g->Data4[0],
                g->Data4[1], g->Data4[2], g->Data4[3], g->Data4[4], g->Data4[5], g->Data4[6],
                g->Data4[7], r->attributes, r->count))
      return EFI_DEVICE_ERROR;
  }
  return EFI_SUCCESS;
}
/* A firmware Start wrapper may connect known console drivers before returning.
   Accept only complete prefixes of the original chain, with the exact agents,
   protocols, counts, shared consoles and driver fingerprints. No unknown owner
   is accepted, and the complete original plan is still verified before boot. */
static EFI_STATUS RecoveryState(GC_CONTEXT *c, const LC_PLAN *p, EFI_HANDLE child, UINT32 *mask) {
  LC_SET set = {0}, shared = {0};
  *mask = 0;
  EFI_STATUS s = Records(c, c->target, &set);
  if (!EFI_ERROR(s)) s = Records(c, child, &set);
  if (!c->log("LC_RECOVERY_SCAN child=%#llx status=%#llx records=%zu\n",
              (unsigned long long)(UINTN)child, (unsigned long long)s, (size_t)set.count))
    return EFI_DEVICE_ERROR;
  if (EFI_ERROR(s)) return s;
  s = LogRecords(c, "target-child", &set);
  if (EFI_ERROR(s)) return s;
  s = MatchShape(c, p, child, &set, mask);
  if (!EFI_ERROR(s) && *mask != 3 && *mask != 7 && *mask != 15 && *mask != 63)
    s = EFI_ACCESS_DENIED;
  EFI_STATUS ss = Shared(c, p, &shared);
  BOOLEAN same = !EFI_ERROR(ss) && SameSet(&shared, &p->shared);
  if (!c->log("LC_RECOVERY_STATE mask=%#x status=%#llx shared_status=%#llx shared_same=%u "
              "input_same=%u output_same=%u error_same=%u\n",
              *mask, (unsigned long long)s, (unsigned long long)ss, same,
              c->table->ConsoleInHandle == p->consoles[0] && c->table->ConIn == p->interfaces[0],
              c->table->ConsoleOutHandle == p->consoles[1] && c->table->ConOut == p->interfaces[1],
              c->table->StandardErrorHandle == p->consoles[2] &&
                  c->table->StdErr == p->interfaces[2]))
    return EFI_DEVICE_ERROR;
  if (EFI_ERROR(LogRecords(c, "shared-current", &shared)) ||
      EFI_ERROR(LogRecords(c, "shared-original", &p->shared)))
    return EFI_DEVICE_ERROR;
  if (EFI_ERROR(s)) return s;
  if (!same) return EFI_ERROR(ss) ? ss : EFI_COMPROMISED_DATA;
  for (UINTN role = 0; role < 4; role++) {
    EFI_DRIVER_BINDING_PROTOCOL *binding = NULL;
    s = DriverNow(c, p, role, &binding);
    if (!c->log("LC_RECOVERY_DRIVER role=%zu status=%#llx\n", (size_t)role, (unsigned long long)s))
      return EFI_DEVICE_ERROR;
    if (EFI_ERROR(s)) return s;
  }
  return EFI_SUCCESS;
}
static EFI_STATUS RestoreConsoles(GC_CONTEXT *c, const LC_PLAN *p, EFI_HANDLE child) {
  UINT32 minimum = 3;
  for (UINTN step = 0; step < 4; step++) {
    UINT32 mask;
    EFI_STATUS s = RecoveryState(c, p, child, &mask);
    if (EFI_ERROR(s)) return s;
    if ((mask & minimum) != minimum) return EFI_COMPROMISED_DATA;
    if (mask == 63) return EFI_SUCCESS;
    UINTN role = mask == 3 ? 1 : mask == 7 ? 2 : 3;
    if (!c->log("LC_RECOVERY_NEXT role=%zu observed=%#x\n", (size_t)role, mask))
      return EFI_DEVICE_ERROR;
    s = Start(c, p, role, child, child, mask);
    if (EFI_ERROR(s)) return s;
    minimum = role == 1 ? 7 : role == 2 ? 15 : 63;
  }
  return EFI_ABORTED;
}
static EFI_STATUS Isolated(GC_CONTEXT *c, const LC_PLAN *p) {
  EFI_STATUS s = Shape(c, p, p->child, 0);
  if (!c->log("LC_ISOLATION_SHAPE status=%#llx\n", (unsigned long long)s)) return EFI_DEVICE_ERROR;
  if (EFI_ERROR(s)) return s;
  EFI_STATUS result = EFI_SUCCESS;
  EFI_GUID *old[] = {&GopGuid, &TextGuid, &PathGuid};
  const char *roles[] = {"gop", "text", "path"};
  for (UINTN i = 0; i < 3; i++) {
    VOID *v = NULL;
    s = c->table->BootServices->HandleProtocol(p->child, old[i], &v);
    BOOLEAN absent = !v && Missing(s);
    if (!c->log("LC_OLD_INTERFACE role=%s handle=%#llx status=%#llx interface=%#llx absent=%u\n",
                roles[i], (unsigned long long)(UINTN)p->child, (unsigned long long)s,
                (unsigned long long)(UINTN)v, absent))
      return EFI_DEVICE_ERROR;
    if (!absent) result = EFI_ACCESS_DENIED;
  }
  EFI_HANDLE *handles = NULL;
  UINTN count = 0;
  s = c->table->BootServices->LocateHandleBuffer(ByProtocol, &GopGuid, NULL, &count, &handles);
  if (!c->log("LC_REMAINING_GOPS status=%#llx count=%zu\n", (unsigned long long)s, (size_t)count))
    return Free(c, handles, EFI_DEVICE_ERROR);
  if (s == EFI_NOT_FOUND) return Free(c, handles, result);
  if (s != EFI_SUCCESS) return Free(c, handles, EFI_ERROR(s) ? s : EFI_DEVICE_ERROR);
  if (!handles || !count || count > 16) return Free(c, handles, EFI_COMPROMISED_DATA);
  if (count != 1) result = EFI_ACCESS_DENIED;
  for (UINTN i = 0; i < count; i++) {
    EFI_GRAPHICS_OUTPUT_PROTOCOL *gop = NULL;
    EFI_DEVICE_PATH_PROTOCOL *path = NULL;
    EFI_STATUS ps = c->table->BootServices->HandleProtocol(handles[i], &PathGuid, (VOID **)&path);
    s = c->table->BootServices->HandleProtocol(handles[i], &GopGuid, (VOID **)&gop);
    BOOLEAN valid_mode = s == EFI_SUCCESS && gop && gop->Mode;
    BOOLEAN approved = valid_mode && handles[i] == p->consoles[1] && !path &&
                       (ps == EFI_UNSUPPORTED || ps == EFI_NOT_FOUND) &&
                       !gop->Mode->FrameBufferBase && !gop->Mode->FrameBufferSize;
    if (!c->log("LC_REMAINING_GOP handle=%#llx console=%u path_status=%#llx path=%#llx "
                "gop_status=%#llx valid_mode=%u framebuffer=%#llx bytes=%zu approved=%u\n",
                (unsigned long long)(UINTN)handles[i], handles[i] == p->consoles[1],
                (unsigned long long)ps, (unsigned long long)(UINTN)path, (unsigned long long)s,
                valid_mode, valid_mode ? (unsigned long long)gop->Mode->FrameBufferBase : 0,
                valid_mode ? (size_t)gop->Mode->FrameBufferSize : 0, approved))
      return Free(c, handles, EFI_DEVICE_ERROR);
    if (!approved) result = EFI_ACCESS_DENIED;
  }
  return Free(c, handles, result);
}
static EFI_STATUS ConfigEvidence(GC_CONTEXT *c, const char *phase) {
  UINT32 cfg[64];
  EFI_STATUS s = c->pci->Pci.Read(c->pci, EfiPciIoWidthUint32, 0, 64, cfg);
  if (s != EFI_SUCCESS) return EFI_ERROR(s) ? s : EFI_DEVICE_ERROR;
  for (UINTN i = 0; i < 64; i++)
    if (!c->log("LC_CONFIG phase=%s offset=%02x value=%08x\n", phase, (unsigned)(i * 4), cfg[i]))
      return EFI_DEVICE_ERROR;
  return cfg[0] == c->identity && cfg[11] == c->subsystem && cfg[2] == c->class_revision &&
                 !(cfg[1] & 4)
             ? EFI_SUCCESS
             : EFI_ACCESS_DENIED;
}
#ifdef GC_LENOVO_VIRTUAL_RELEASE
/* No console, file or log calls: also checked at TPL_NOTIFY. The original
   interface object stays allocated and unmodified throughout this experiment. */
static EFI_STATUS WithdrawnClear(GC_CONTEXT *c, const LC_PLAN *p) {
  EFI_BOOT_SERVICES *bs = c->table->BootServices;
  if (!VirtualMemorySame(p)) return EFI_COMPROMISED_DATA;
  EFI_GUID *old[] = {&GopGuid, &TextGuid, &PathGuid};
  for (UINTN i = 0; i < 3; i++) {
    VOID *v = NULL;
    EFI_STATUS s = bs->HandleProtocol(p->child, old[i], &v);
    if (v || !Missing(s)) return EFI_ACCESS_DENIED;
  }
  EFI_HANDLE *handles = NULL;
  UINTN count = 0;
  EFI_STATUS s = bs->LocateHandleBuffer(ByProtocol, &GopGuid, NULL, &count, &handles);
  return Free(c, handles,
              s == EFI_NOT_FOUND && !count && !handles ? EFI_SUCCESS
              : s == EFI_SUCCESS                       ? EFI_ACCESS_DENIED
              : EFI_ERROR(s)                           ? s
                                                       : EFI_DEVICE_ERROR);
}
static EFI_STATUS VirtualCandidate(GC_CONTEXT *c, const LC_PLAN *p) {
  EFI_BOOT_SERVICES *bs = c->table->BootServices;
  EFI_STATUS s = Shape(c, p, p->child, 0);
  if (EFI_ERROR(s)) return s;
  if (!VirtualMemorySame(p) || !VirtualMethodsInDriver(p, p->virtual_gop)) return EFI_ACCESS_DENIED;
  EFI_GUID *old[] = {&GopGuid, &TextGuid, &PathGuid};
  for (UINTN i = 0; i < 3; i++) {
    VOID *v = NULL;
    s = bs->HandleProtocol(p->child, old[i], &v);
    if (v || !Missing(s)) return EFI_ACCESS_DENIED;
  }
  LC_SET set = {0};
  s = Records(c, p->consoles[1], &set);
  if (EFI_ERROR(s)) return s;
  if (set.count) return EFI_ACCESS_DENIED;
  for (UINTN role = 0; role < 4; role++) {
    EFI_DRIVER_BINDING_PROTOCOL *binding = NULL;
    s = DriverNow(c, p, role, &binding);
    if (EFI_ERROR(s)) return s;
  }
  EFI_DEVICE_PATH_PROTOCOL *path = NULL;
  s = bs->HandleProtocol(p->consoles[1], &PathGuid, (VOID **)&path);
  if (path || (s != EFI_NOT_FOUND && s != EFI_UNSUPPORTED)) return EFI_ACCESS_DENIED;
  EFI_GRAPHICS_OUTPUT_PROTOCOL *gop = NULL;
  s = bs->HandleProtocol(p->consoles[1], &GopGuid, (VOID **)&gop);
  if (s != EFI_SUCCESS || gop != p->virtual_gop) return EFI_ACCESS_DENIED;
  EFI_HANDLE *handles = NULL;
  UINTN count = 0;
  s = bs->LocateHandleBuffer(ByProtocol, &GopGuid, NULL, &count, &handles);
  if (s == EFI_SUCCESS && (!handles || count != 1 || handles[0] != p->consoles[1]))
    s = EFI_ACCESS_DENIED;
  return Free(c, handles, s == EFI_SUCCESS || EFI_ERROR(s) ? s : EFI_DEVICE_ERROR);
}
static EFI_STATUS WithdrawVirtual(GC_CONTEXT *c, const LC_PLAN *p, BOOLEAN *attempted,
                                  BOOLEAN *withdrawn) {
  *attempted = FALSE;
  *withdrawn = FALSE;
  EFI_STATUS s = VirtualCandidate(c, p);
  if (!c->log("LC_VIRTUAL_CANDIDATE status=%#llx handle=%#llx interface=%#llx\n",
              (unsigned long long)s, (unsigned long long)(UINTN)p->consoles[1],
              (unsigned long long)(UINTN)p->virtual_gop))
    return EFI_DEVICE_ERROR;
  if (EFI_ERROR(s)) return s;
  if (!c->log("LC_VIRTUAL_WITHDRAW_INTENT: exact ownerless cached interface; object retained\n"))
    return EFI_DEVICE_ERROR;
  EFI_BOOT_SERVICES *bs = c->table->BootServices;
  EFI_TPL old = bs->RaiseTPL(TPL_NOTIFY);
  s = VirtualCandidate(c, p);
  if (!EFI_ERROR(s)) {
    *attempted = TRUE;
    s = bs->UninstallProtocolInterface(p->consoles[1], &GopGuid, p->virtual_gop);
    *withdrawn = s == EFI_SUCCESS;
  }
  bs->RestoreTPL(old);
  if (!c->log("LC_VIRTUAL_WITHDRAW_RESULT status=%#llx attempted=%u withdrawn=%u\n",
              (unsigned long long)s, *attempted, *withdrawn))
    return EFI_DEVICE_ERROR;
  return s == EFI_SUCCESS || EFI_ERROR(s) ? s : EFI_DEVICE_ERROR;
}
static EFI_STATUS RepublishVirtual(GC_CONTEXT *c, const LC_PLAN *p) {
  EFI_BOOT_SERVICES *bs = c->table->BootServices;
  if (!c->log("LC_VIRTUAL_RESTORE_INTENT: same handle, interface and original mode\n"))
    return EFI_DEVICE_ERROR;
  EFI_TPL old = bs->RaiseTPL(TPL_NOTIFY);
  EFI_STATUS s = Shape(c, p, p->child, 0);
  EFI_GRAPHICS_OUTPUT_PROTOCOL *gop = NULL;
  EFI_STATUS gs = bs->HandleProtocol(p->consoles[1], &GopGuid, (VOID **)&gop);
  if (!EFI_ERROR(s) &&
      (gop || (gs != EFI_NOT_FOUND && gs != EFI_UNSUPPORTED) || !VirtualMemorySame(p)))
    s = EFI_COMPROMISED_DATA;
  EFI_HANDLE console = p->consoles[1];
  if (!EFI_ERROR(s))
    s = bs->InstallProtocolInterface(&console, &GopGuid, EFI_NATIVE_INTERFACE, p->virtual_gop);
  if (s == EFI_SUCCESS && console != p->consoles[1]) s = EFI_COMPROMISED_DATA;
  bs->RestoreTPL(old);
  gop = NULL;
  gs = bs->HandleProtocol(p->consoles[1], &GopGuid, (VOID **)&gop);
  if (s == EFI_SUCCESS && (gs != EFI_SUCCESS || gop != p->virtual_gop || !VirtualMemorySame(p)))
    s = EFI_COMPROMISED_DATA;
  if (!c->log("LC_VIRTUAL_RESTORE_RESULT status=%#llx query=%#llx same_interface=%u\n",
              (unsigned long long)s, (unsigned long long)gs, gop == p->virtual_gop))
    return EFI_DEVICE_ERROR;
  return s == EFI_SUCCESS || EFI_ERROR(s) ? s : EFI_DEVICE_ERROR;
}
#endif
#ifdef GC_LENOVO_UNLOCK
static GC_CONTEXT *PatchContext;
static const LC_PLAN *PatchPlan;
/* The only active target record during firmware execution must be this image's
   exclusive claim. No console output or GPU access occurs in this callback. */
static BOOLEAN PatchIsolated(VOID) {
  GC_CONTEXT *c = PatchContext;
  const LC_PLAN *p = PatchPlan;
  if (!c || !p) return FALSE;
  EFI_BOOT_SERVICES *bs = c->table->BootServices;
  EFI_TPL old = bs->RaiseTPL(TPL_NOTIFY);
  LC_SET set = {0};
  EFI_STATUS s = Records(c, c->target, &set);
  if (!EFI_ERROR(s)) {
    const LC_RECORD *r = &set.records[0];
    if (set.count != 1 || r->handle != c->target || !Eq(&r->protocol, &PciGuid) ||
        r->agent != c->image || r->controller || r->count != 1 ||
        r->attributes != EFI_OPEN_PROTOCOL_EXCLUSIVE)
      s = EFI_ACCESS_DENIED;
  }
  if (!EFI_ERROR(s)) s = SharedUnchanged(c, p);
  if (!EFI_ERROR(s)) s = WithdrawnClear(c, p);
  for (UINTN role = 0; !EFI_ERROR(s) && role < 4; role++) {
    EFI_DRIVER_BINDING_PROTOCOL *binding = NULL;
    s = DriverNow(c, p, role, &binding);
  }
  bs->RestoreTPL(old);
  return s == EFI_SUCCESS;
}
#endif
#ifdef GC_LENOVO_WINDOWS_TAKEOVER
/* FLR can change read-only status bits. Windows needs the original resources
   and control settings, D0, disabled MSI/MSI-X and no outstanding transaction;
   GfSavePci checks the latter ownership-independent operating conditions. */
static BOOLEAN PciReadyForWindows(const GF_PCI *before, const GF_PCI *after) {
  if (before->pcie != after->pcie || before->version != after->version ||
      before->control != after->control || before->control2 != after->control2 ||
      (before->config[1] & 0xffff) != (after->config[1] & 0xffff) ||
      (after->config[(after->pcie + 8) / 4] & 0x00200000U))
    return FALSE;
  const UINTN stable[] = {0, 2, 3, 4, 5, 6, 7, 8, 9, 11, 12, 13};
  for (UINTN i = 0; i < sizeof(stable) / sizeof(stable[0]); i++)
    if (before->config[stable[i]] != after->config[stable[i]]) return FALSE;
  return TRUE;
}
#endif
EFI_STATUS LcRun(GC_CONTEXT *c, const LC_PLAN *p, GC_RESULT *r) {
  memset(r, 0, sizeof(*r));
  r->detach = r->isolated = r->claim = r->close = r->connect = r->verify = EFI_NOT_STARTED;
#ifdef GC_LENOVO_UNLOCK
  r->patch = r->observe_before = r->observe_after = EFI_NOT_STARTED;
  PatchContext = NULL;
  PatchPlan = NULL;
#endif
  LC_PLAN *fresh = NULL;
#ifdef GC_LENOVO_VIRTUAL_RELEASE
  BOOLEAN virtual_withdrawn = FALSE, virtual_restored = FALSE;
#endif
  EFI_BOOT_SERVICES *bs = c->table->BootServices;
  EFI_STATUS s = bs->AllocatePool(EfiLoaderData, sizeof(*fresh), (VOID **)&fresh);
  if (s != EFI_SUCCESS || !fresh) return EFI_OUT_OF_RESOURCES;
  s = LcPrepare(c, fresh);
  if (!EFI_ERROR(s) && !SamePlan(p, fresh, TRUE)) s = EFI_ABORTED;
  if (EFI_ERROR(s)) goto finish;
  EFI_HANDLE shared = p->consoles[1], child = p->child;
  s = Stop(c, p, 3, child, 1, &shared, 63, 15, r);
  if (!EFI_ERROR(s)) s = Stop(c, p, 2, child, 0, NULL, 15, 7, r);
  if (!EFI_ERROR(s)) s = Stop(c, p, 1, child, 0, NULL, 7, 3, r);
  if (!EFI_ERROR(s)) s = Stop(c, p, 0, c->target, 1, &child, 3, 1, r);
  if (!EFI_ERROR(s)) s = Stop(c, p, 0, c->target, 0, NULL, 1, 0, r);
  r->detach = s;
  if (EFI_ERROR(s)) goto finish;
  s = Isolated(c, p);
  r->isolated = s;
#ifdef GC_LENOVO_VIRTUAL_RELEASE
  if (s == EFI_ACCESS_DENIED) {
    BOOLEAN attempted = FALSE;
    EFI_STATUS vs = WithdrawVirtual(c, p, &attempted, &virtual_withdrawn);
    if (vs == EFI_SUCCESS) {
      s = Isolated(c, p);
      r->isolated = s;
      if (EFI_ERROR(s)) goto finish;
    } else if (attempted || vs != EFI_ACCESS_DENIED) {
      s = vs;
      goto finish;
    }
  }
#endif
  /* Recovery-only build: metadata refusal does not authorize a claim/unlock.
     All five Stop calls must have succeeded. Recheck idle ownership, unchanged
     shared consoles and valid PCI identity/BME before restoring the saved drivers.
     Errors from Stop, I/O, logging or a partial claim still halt the experiment. */
#ifdef GC_LENOVO_CYCLE
  if (s == EFI_ACCESS_DENIED) {
    if (!c->log("LC_ISOLATION_DECLINED: claim and unlock skipped; checking baseline recovery\n")) {
      s = EFI_DEVICE_ERROR;
      goto finish;
    }
    s = Shape(c, p, child, 0);
    if (!EFI_ERROR(s)) s = ConfigEvidence(c, "recovery-without-claim");
    if (EFI_ERROR(s)) goto finish;
    goto recover;
  }
#endif
  if (!EFI_ERROR(s)) s = ConfigEvidence(c, "isolated");
  if (EFI_ERROR(s)) goto finish;
  if (!c->log(
#ifdef GC_LENOVO_UNLOCK
          "LC_CLAIM_INTENT: exclusive target ownership for one SS0-only unlock transaction\n"
#else
          "LC_CLAIM_INTENT no owners or display interfaces; no unlock or FLR\n"
#endif
          )) {
    s = EFI_DEVICE_ERROR;
    goto finish;
  }
  EFI_TPL old = bs->RaiseTPL(TPL_NOTIFY);
  s = Shape(c, p, child, 0);
#ifdef GC_LENOVO_VIRTUAL_RELEASE
  if (!EFI_ERROR(s) && virtual_withdrawn) s = WithdrawnClear(c, p);
#endif
  EFI_PCI_IO_PROTOCOL *claimed = NULL;
  if (!EFI_ERROR(s))
    s = bs->OpenProtocol(c->target, &PciGuid, (VOID **)&claimed, c->image, NULL,
                         EFI_OPEN_PROTOCOL_EXCLUSIVE);
  bs->RestoreTPL(old);
  r->claim = s;
  if (EFI_ERROR(s)) goto finish;
  s = claimed == c->pci ? EFI_SUCCESS : EFI_COMPROMISED_DATA;
#ifdef GC_LENOVO_UNLOCK
  if (EFI_ERROR(s)) goto finish;
  PatchContext = c;
  PatchPlan = p;
  if (!PatchIsolated() || !c->log("LC_PATCH_INTENT: SS0-only; preserve SS1; one built-in FLR\n")) {
    s = EFI_ACCESS_DENIED;
    goto finish;
  }
  r->unlock_attempted = TRUE;
  s = r->patch = GxPatch(PatchIsolated);
  if (!c->log("LC_PATCH_RESULT status=%#llx safe=%u\n", (unsigned long long)s, GxSafe()))
    s = EFI_DEVICE_ERROR;
  if (EFI_ERROR(s) || !GxSafe()) {
    s = EFI_ERROR(s) ? s : EFI_DEVICE_ERROR;
    goto finish;
  }
  s = r->observe_before = GxObserve(c, "before-gop");
  if (EFI_ERROR(s)) goto finish;
#endif
  r->close = bs->CloseProtocol(c->target, &PciGuid, c->image, NULL);
  if (EFI_ERROR(r->close)) s = r->close;
#ifdef GC_LENOVO_UNLOCK
  if (!EFI_ERROR(r->close)) GxReleased();
#endif
  if (!c->log("LC_CLAIM_RESULT claim=%#llx same_pci=%u close=%#llx\n", (unsigned long long)r->claim,
              claimed == c->pci, (unsigned long long)r->close))
    s = EFI_DEVICE_ERROR;
  if (EFI_ERROR(s)) goto finish;
#ifdef GC_LENOVO_WINDOWS_TAKEOVER
  /* v11 proved that the unlock/FLR completes, but the firmware NVIDIA Start
     returns EFI_NOT_STARTED. Leave the target unbound for the Windows driver.
     No working GOP is advertised and no stale framebuffer is accessed. */
  s = virtual_withdrawn ? Shape(c, p, p->child, 0) : EFI_ACCESS_DENIED;
  if (!EFI_ERROR(s)) s = WithdrawnClear(c, p);
  if (!EFI_ERROR(s)) s = ConfigEvidence(c, "windows-takeover");
  if (!EFI_ERROR(s)) s = GfSavePci(c, &fresh->pci);
  BOOLEAN pci_ready = !EFI_ERROR(s) && PciReadyForWindows(&p->pci, &fresh->pci);
  if (!EFI_ERROR(s) && !pci_ready) s = EFI_COMPROMISED_DATA;
  if (!EFI_ERROR(s)) s = r->observe_after = GxObserve(c, "windows-takeover");
  if (!EFI_ERROR(s) && !GxSafe()) s = EFI_DEVICE_ERROR;
  if (!c->log("LC_WINDOWS_TAKEOVER_READY status=%#llx require_no_gop=1 require_unbound=1 "
              "pci_operational_match=%u firmware_display_restart=0\n",
              (unsigned long long)s, pci_ready))
    s = EFI_DEVICE_ERROR;
  r->verify = s;
  r->windows_ready = r->unlock_verified = s == EFI_SUCCESS;
  goto finish;
#endif
#ifdef GC_LENOVO_CYCLE
recover:
#endif
#ifdef GC_LENOVO_VIRTUAL_RELEASE
  if (virtual_withdrawn) {
    s = RepublishVirtual(c, p);
    virtual_restored = s == EFI_SUCCESS;
    if (EFI_ERROR(s)) goto finish;
  }
  s = Shape(c, p, child, 0);
  if (virtual_restored && s == EFI_ACCESS_DENIED) {
    /* Installing GOP can run firmware notifications. Only the same qualified
       root owner and full known console prefixes may already have reconnected. */
    EFI_HANDLE owner = NULL;
    s = Root(c, &owner, &child);
    if (!EFI_ERROR(s) && owner != p->drivers[0].driver.handle) s = EFI_ACCESS_DENIED;
    UINT32 mask = 0;
    if (!EFI_ERROR(s)) s = RecoveryState(c, p, child, &mask);
    if (!c->log("LC_VENDOR_ALREADY_RECONNECTED status=%#llx mask=%#x\n", (unsigned long long)s,
                mask))
      s = EFI_DEVICE_ERROR;
  } else if (!EFI_ERROR(s)) {
    s = Start(c, p, 0, c->target, child, 0);
  }
#else
  s = Start(c, p, 0, c->target, child, 0);
#endif
  r->connect = s;
  if (EFI_ERROR(s)) goto finish;
  EFI_HANDLE owner = NULL;
  s = Root(c, &owner, &child);
  if (!c->log("LC_RECOVERY_ROOT status=%#llx owner=%#llx child=%#llx\n", (unsigned long long)s,
              (unsigned long long)(UINTN)owner, (unsigned long long)(UINTN)child))
    s = EFI_DEVICE_ERROR;
  if (!EFI_ERROR(s) && owner != p->drivers[0].driver.handle) s = EFI_COMPROMISED_DATA;
  if (!EFI_ERROR(s)) s = RestoreConsoles(c, p, child);
  r->connect = s;
  if (EFI_ERROR(s)) goto finish;
  s = ConfigEvidence(c, "restored");
  if (!EFI_ERROR(s)) s = LcPrepare(c, fresh);
  if (!EFI_ERROR(s)) {
    BOOLEAN same = SamePlan(p, fresh, FALSE);
    if (!c->log("LC_RESTORE_COMPARE same_plan=%u pci_same=%u info_same=%u old_child=%#llx "
                "new_child=%#llx old_mode=%u new_mode=%u\n",
                same, !memcmp(&p->pci, &fresh->pci, sizeof(p->pci)),
                !memcmp(&p->info, &fresh->info, sizeof(p->info)),
                (unsigned long long)(UINTN)p->child, (unsigned long long)(UINTN)fresh->child,
                p->mode, fresh->mode))
      s = EFI_DEVICE_ERROR;
    else if (!same)
      s = EFI_COMPROMISED_DATA;
  }
#ifdef GC_LENOVO_UNLOCK
  if (!EFI_ERROR(s) && r->unlock_attempted) {
    s = r->observe_after = GxObserve(c, "after-gop");
    r->unlock_verified = s == EFI_SUCCESS;
  }
#endif
  r->verify = s;
  r->restored = !EFI_ERROR(s);
  if (r->restored && EFI_ERROR(r->isolated) &&
      !c->log("LC_BASELINE_RECOVERED: original display state verified; isolation NOT qualified; "
              "no claim, unlock or reset attempted\n"))
    s = EFI_DEVICE_ERROR;
finish:
#ifdef GC_LENOVO_VIRTUAL_RELEASE
  if (!c->log("LC_VIRTUAL_FINAL withdrawn=%u republished=%u\n", virtual_withdrawn,
              virtual_restored))
    s = EFI_DEVICE_ERROR;
#endif
  if (!c->log("LC_FINAL status=%#llx touched=%u restored=%u detach=%#llx isolated=%#llx "
              "claim=%#llx close=%#llx reconnect=%#llx verify=%#llx\n",
              (unsigned long long)s, r->touched, r->restored, (unsigned long long)r->detach,
              (unsigned long long)r->isolated, (unsigned long long)r->claim,
              (unsigned long long)r->close, (unsigned long long)r->connect,
              (unsigned long long)r->verify))
    s = EFI_DEVICE_ERROR;
#ifdef GC_LENOVO_UNLOCK
  if (!c->log("LC_UNLOCK_FINAL attempted=%u verified=%u windows_ready=%u patch=%#llx before=%#llx "
              "after=%#llx\n",
              r->unlock_attempted, r->unlock_verified, r->windows_ready,
              (unsigned long long)r->patch, (unsigned long long)r->observe_before,
              (unsigned long long)r->observe_after))
    s = EFI_DEVICE_ERROR;
  PatchContext = NULL;
  PatchPlan = NULL;
#endif
  return Free(c, fresh, s);
}
