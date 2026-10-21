//! @file
//! @brief avr/io.h replacement for the STM32G0 port.
//! There are no AVR special function registers on the STM32. Code that touched
//! registers directly has been ported to the HAL in hal_stm32/hal.h.
#pragma once

#include <stdint.h>
#include "../../hal.h"
