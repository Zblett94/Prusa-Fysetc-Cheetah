#pragma once
#include <stdint.h>
#include <stddef.h>
#if defined(__AVR__) || defined(PRUSA_MK3_FW)
    #include "mmu2/progress_codes.h"
#else
    #include "../../../../../../Prusa-Firmware-MMU/src/logic/progress_codes.h"
#endif

namespace MMU2 {

const char *ProgressCodeToText(ProgressCode pc);

}
