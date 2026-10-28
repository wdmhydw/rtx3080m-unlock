#pragma once
#include "cycle.h"
#include "reset.h"

#define LC_RECORDS 32
typedef struct {
  EFI_HANDLE handle, agent, controller;
  EFI_GUID protocol;
  UINT32 attributes, count;
} LC_RECORD;
typedef struct {
  LC_RECORD records[LC_RECORDS];
  UINTN count;
} LC_SET;
typedef struct {
  GC_DRIVER driver;
  EFI_DRIVER_BINDING_SUPPORTED supported;
} LC_DRIVER;
typedef struct {
  EFI_HANDLE child, consoles[3];
  VOID *interfaces[3];
  LC_DRIVER drivers[4]; /* NVIDIA, GraphicsConsole, ConPlatform, ConSplitter */
  LC_SET shared;
  EFI_GRAPHICS_OUTPUT_PROTOCOL *gop;
  EFI_GRAPHICS_OUTPUT_MODE_INFORMATION info;
  EFI_PHYSICAL_ADDRESS framebuffer;
  UINTN framebuffer_size, info_size;
  UINT32 mode, max_mode;
  GF_PCI pci;
#ifdef GC_LENOVO_VIRTUAL_RELEASE
  EFI_GRAPHICS_OUTPUT_PROTOCOL *virtual_gop;
  EFI_GRAPHICS_OUTPUT_PROTOCOL virtual_interface;
  EFI_GRAPHICS_OUTPUT_PROTOCOL_MODE virtual_mode;
#endif
} LC_PLAN;
EFI_STATUS LcPrepare(GC_CONTEXT *context, LC_PLAN *plan);
EFI_STATUS LcRun(GC_CONTEXT *context, const LC_PLAN *plan, GC_RESULT *result);
