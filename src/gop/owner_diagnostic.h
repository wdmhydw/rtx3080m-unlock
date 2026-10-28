#pragma once
#include "cycle.h"
/* Collect driver metadata and Windows loadability without releasing a driver,
   acquiring exclusive access, resetting a GPU, or starting Windows. */
EFI_STATUS GdCollectOwnerDiagnostic(GC_CONTEXT *context);
