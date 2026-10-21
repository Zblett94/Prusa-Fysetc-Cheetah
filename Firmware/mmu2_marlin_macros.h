/// @file
/// This file will not be the same on Marlin1 and Marlin2.
/// Its purpose is to unify different macros in either of Marlin incarnations.
#pragma once

#if defined(__AVR__) || defined(PRUSA_MK3_FW)
    #include "Marlin.h"
    // brings _O and _T macros into MMU
    #include "language.h"
    #define MARLIN_KEEPALIVE_STATE_IN_PROCESS KEEPALIVE_STATE(IN_PROCESS)
#elif defined(UNITTEST)
    #define _O(x)                             x
    #define _T(x)                             x
    #define MARLIN_KEEPALIVE_STATE_IN_PROCESS /*KEEPALIVE_STATE(IN_PROCESS) TODO*/
#else
    #include "../../gcode/gcode.h"
    #define _O(x)                             x
    #define _T(x)                             x
    #define MARLIN_KEEPALIVE_STATE_IN_PROCESS KEEPALIVE_STATE(IN_PROCESS)
#endif
