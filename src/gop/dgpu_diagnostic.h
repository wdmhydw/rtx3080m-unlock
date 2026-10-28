#pragma once
#include "cycle.h"
#include <Protocol/SimpleFileSystem.h>

EFI_STATUS GdReadDisplays(GC_CONTEXT *context);
EFI_STATUS GdCaptureAndBoot(GC_CONTEXT *context, EFI_FILE_PROTOCOL **report);
