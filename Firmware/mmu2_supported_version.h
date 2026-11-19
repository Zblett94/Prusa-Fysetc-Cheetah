#pragma once
#include <stdint.h>

namespace MMU2 {

static constexpr uint8_t mmuVersionMajor = 3;
static constexpr uint8_t mmuVersionMinor = 0;
static constexpr uint8_t mmuVersionPatch = 3;
// STM32 port: MMU FW 3.0.4 (the version of the MK3.5/MK4 firmware, December 2025) is
// accepted too: same protocol and registers as 3.0.3, only internal changes of the MMU
// (tool change, unload, LEDs, writable accelerations, the Mode command).
static constexpr uint8_t mmuVersionPatchMax = 4;

} // namespace MMU2
