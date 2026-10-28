#pragma once
#include "ownership.h"
typedef struct {
  EFI_SYSTEM_TABLE *table;
  EFI_HANDLE target, image;
  EFI_PCI_IO_PROTOCOL *pci;
  GU_RELEASE_OWNER owner;
  BOOLEAN ready, release, consumed;
} GU_SINGLE_OWNER_PLAN;
EFI_STATUS GuSingleOwnerPrepare(EFI_SYSTEM_TABLE *table, GU_SINGLE_OWNER_PLAN *plan);
/* Invoke after the selected approval policy and a fresh display/config check. */
EFI_STATUS GuSingleOwnerRelease(EFI_SYSTEM_TABLE *table, GU_SINGLE_OWNER_PLAN *plan,
                                BOOLEAN *touched, EFI_STATUS *detach);
