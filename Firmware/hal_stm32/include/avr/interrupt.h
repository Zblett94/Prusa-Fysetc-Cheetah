//! @file
//! @brief avr/interrupt.h replacement for the STM32G0 port.
#pragma once

#include <stdint.h>
#include <stm32_def.h>
#undef CRC // CMSIS CRC peripheral macro, clashes with the MMU protocol code

#ifndef cli
#define cli() __disable_irq()
#endif
#ifndef sei
#define sei() __enable_irq()
#endif

// The AVR code saves SREG (incl. the global interrupt flag) and restores it
// afterwards. PRIMASK is the Cortex-M0+ equivalent of the SREG I-bit.
#define CRITICAL_SECTION_START  uint32_t _sreg = __get_PRIMASK(); __disable_irq();
#define CRITICAL_SECTION_END    __set_PRIMASK(_sreg);
