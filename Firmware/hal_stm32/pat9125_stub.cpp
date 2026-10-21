//! @file
//! @brief The PAT9125 (MK3 laser filament sensor) can not be connected to the SKR Mini E3 V3
//! (no free I2C bus). The IR filament sensor code only probes for it, so report "not present".

#include <stdint.h>
#include "../pat9125.h"

uint8_t pat9125_probe() { return 0; }
