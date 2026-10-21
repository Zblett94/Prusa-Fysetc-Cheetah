//! @file
//! @brief avr/wdt.h replacement: maps the AVR watchdog onto the STM32 IWDG.
#pragma once

#include <stdint.h>

#define WDTO_15MS   0
#define WDTO_30MS   1
#define WDTO_60MS   2
#define WDTO_120MS  3
#define WDTO_250MS  4
#define WDTO_500MS  5
#define WDTO_1S     6
#define WDTO_2S     7
#define WDTO_4S     8
#define WDTO_8S     9

#ifdef __cplusplus
extern "C" {
#endif

//! Start the independent watchdog with the AVR timeout code @p wdto.
//! Note: unlike the AVR WDT the IWDG can not be stopped once started.
void wdt_enable(uint8_t wdto);
void wdt_reset(void);
//! The IWDG can not be stopped; this sets the longest timeout instead.
void wdt_disable(void);

#ifdef __cplusplus
}
#endif
