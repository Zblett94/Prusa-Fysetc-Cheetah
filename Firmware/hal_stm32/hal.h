//! @file
//! @brief Hardware abstraction for the STM32G0B1 (BigTreeTech SKR Mini E3 V3.0)
//!
//! The Prusa firmware was written for the ATmega2560. Instead of rewriting the
//! printer logic, the AVR peripherals it uses are mapped onto STM32 peripherals
//! with the same timing:
//!
//! | AVR (Einsy)                     | STM32G0B1 (SKR Mini E3 V3)                    |
//! |---------------------------------|-----------------------------------------------|
//! | Timer1 CTC @ 2 MHz (stepper)    | TIM17 @ 2 MHz, ARR = OCR1A, CNT = TCNT1        |
//! | Timer2 COMPB ~976 Hz (soft PWM) | TIM16 @ 976.5625 Hz                            |
//! | Timer5 COMPA ~3.7 Hz (temp mgr) | TIM15 @ 3.7 Hz                                 |
//! | Timer0 fast PWM (heatbed)       | TIM3 CH4 (PC9) fast PWM, same automaton        |
//! | Timer4 (tone)                   | TIM4 via the Arduino tone()                    |
//! | ADC + ADC ISR (10 bit)          | ADC1 + EOC ISR (12 bit, scaled to 10 bit)      |
//! | INT4 / INT7 (UVLO / fan tach)   | EXTI lines                                     |
//! | internal EEPROM 4 KiB           | AT24C32 I2C EEPROM 4 KiB (RAM mirrored)        |
//! | USART0 (USB via 32u2)           | native USB CDC                                 |
//! | USART2 (MMU)                    | USART2 (PA2/PA3, TFT header)                   |
//! | SPI TMC2130                     | single wire UART TMC2209 (USART4)              |
//! | WDT                             | IWDG                                           |
//!
//! Interrupt priorities (Cortex-M0+ has 4 levels, 0 = highest). They replace the
//! AVR "re-enable interrupts inside the ISR" pattern (NONATOMIC_BLOCK):
//!   0 stepper, 1 USB/UART, 2 soft PWM / heatbed / UVLO / tach, 3 temperature manager
#pragma once

#include <stdint.h>
#include <stdbool.h>
#include <stm32_def.h>
#undef CRC // CMSIS CRC peripheral macro, clashes with the MMU protocol code
#include "fastio_stm32.h"

#ifndef F_CPU
#define F_CPU 64000000UL
#endif

// RAM boundaries (used by the D2 memory dump D-code)
#define RAMSTART          0x20000000UL
#define RAMEND            (0x20000000UL + 144UL * 1024UL - 1)

#define HAL_PRIO_STEPPER  0
#define HAL_PRIO_COMM     1
#define HAL_PRIO_SOFTPWM  2
#define HAL_PRIO_EXTI     2
#define HAL_PRIO_TEMPMGR  3

//! Stepper timer. Runs at 2 MHz like Timer1 with the /8 prescaler on a 16 MHz AVR,
//! so the stepper/planner timing math and speed_lookuptable work unchanged.
#define STEP_TIMER        TIM17
#define STEP_TIMER_FREQ   2000000UL
#define OCR1A             (STEP_TIMER->ARR)
#define TCNT1             (STEP_TIMER->CNT)

#ifdef __cplusplus
extern "C" {
#endif

typedef void (*hal_isr_t)(void);

//! Early initialization: GPIO clocks, watchdog/reset cause.
void hal_init(void);

//! Reset cause flags in AVR MCUSR format (PORF/EXTRF/BORF/WDRF)
uint8_t hal_reset_cause(void);
#define HAL_RST_PORF  0x01
#define HAL_RST_EXTRF 0x02
#define HAL_RST_BORF  0x04
#define HAL_RST_WDRF  0x08

void hal_step_timer_init(hal_isr_t isr);
void hal_step_timer_isr_enable(bool enable);

void hal_softpwm_timer_init(hal_isr_t isr);
void hal_softpwm_isr_enable(bool enable);

void hal_tempmgr_timer_init(hal_isr_t isr);
void hal_tempmgr_isr_enable(bool enable);

//! External (pin change) interrupt. mode: RISING, FALLING or CHANGE (Arduino constants)
void hal_exti_attach(uint32_t pin, uint32_t mode, hal_isr_t isr);
void hal_exti_enable(uint32_t pin, bool enable);
bool hal_exti_enabled(uint32_t pin);
//! Change the sensing edge of an attached EXTI line.
void hal_exti_set_edge(uint32_t pin, bool rising, bool falling);

//! Power panic (UVLO) line lock: masks the UVLO EXTI line while a shared bus
//! (TMC UART, EEPROM I2C) transaction is in progress in thread context, so the
//! power panic handler always finds the buses idle.
void hal_uvlo_lock(void);
void hal_uvlo_unlock(void);

//! Hotend (extruder) fan PWM (replacement of timer4_set_fan0)
void hal_hotend_fan_set(uint8_t duty);
//! called from the soft PWM tick
void hal_hotend_fan_tick(void);

//! Reboot the MCU
void hal_reset(void) __attribute__((noreturn));

//! Keep a flash string pointer across a reset (TAMP backup registers)
void hal_kill_message_store(const char *msg);
//! Return the stored pointer if its low 16 bits match @p low16, else @p fallback
const char *hal_kill_message_load(uint16_t low16, const char *fallback);

//! true when executing in an interrupt handler
static inline bool hal_in_isr(void) { return (__get_IPSR() & 0x3F) != 0; }

// stack and heap boundaries (from the linker script)
extern char _end;     // end of .bss (heap start)
extern char _estack;  // top of RAM
extern char _sdata;   // start of RAM data

#ifdef __cplusplus
}
#endif
