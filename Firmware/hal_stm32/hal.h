//! @file
//! @brief Hardware abstraction for the STM32G0B1 (BigTreeTech SKR Mini E3 V3.0) and the
//! STM32F446 (FYSETC Cheetah V3.0)
//!
//! The Prusa firmware was written for the ATmega2560. Instead of rewriting the
//! printer logic, the AVR peripherals it uses are mapped onto STM32 peripherals
//! with the same timing:
//!
//! | AVR (Einsy)                     | STM32G0B1 (SKR Mini E3 V3)      | STM32F446 (Cheetah V3)          |
//! |---------------------------------|---------------------------------|---------------------------------|
//! | Timer1 CTC @ 2 MHz (stepper)    | TIM17 @ 2 MHz                   | TIM7 @ 2 MHz                    |
//! | Timer2 COMPB ~976 Hz (soft PWM) | TIM16 @ 976.5625 Hz             | TIM13 @ 976.5625 Hz             |
//! | Timer5 COMPA ~3.7 Hz (temp mgr) | TIM15 @ 3.7 Hz                  | TIM14 @ 3.7 Hz                  |
//! | Timer0 fast PWM (heatbed)       | TIM1 CH2 (PC9)                  | TIM3 CH3 (PC8)                  |
//! | Timer4 (tone)                   | TIM4 via the Arduino tone()     | TIM6 via the Arduino tone()     |
//! | ADC + ADC ISR (10 bit)          | ADC1 + EOC ISR (12 bit -> 10)   | ADC1 + EOC ISR (12 bit -> 10)   |
//! | INT4 / INT7 (UVLO / fan tach)   | EXTI lines                      | EXTI lines                      |
//! | internal EEPROM 4 KiB           | AT24C32 I2C EEPROM (RAM mirror) | flash sectors 1+2 (RAM mirror)  |
//! | USART0 (USB via 32u2)           | native USB CDC                  | native USB CDC                  |
//! | USART2 (MMU)                    | USART2 (PA2/PA3, TFT header)    | USART1 (PA9/PA10, Pi header)    |
//! | SPI TMC2130                     | TMC2209 UART (USART4)           | TMC2209 UART (bit-banged, PB3)  |
//! | WDT                             | IWDG                            | IWDG                            |
//!
//! Interrupt priorities (0 = highest; the Cortex-M0+ has 4 levels, the M4 16, only
//! 0..3 are used). They replace the AVR "re-enable interrupts inside the ISR"
//! pattern (NONATOMIC_BLOCK):
//!   0 stepper, 1 USB/UART, 2 soft PWM / heatbed / UVLO / tach, 3 temperature manager
#pragma once

#include <stdint.h>
#include <stdbool.h>
#include <stm32_def.h>
#undef CRC // CMSIS CRC peripheral macro, clashes with the MMU protocol code
#include "fastio_stm32.h"

#if defined(STM32G0xx)
#ifndef F_CPU
#define F_CPU 64000000UL
#endif
#define HAL_RAM_SIZE      (144UL * 1024UL)
#define HAL_FLASH_SIZE    (512UL * 1024UL)
#elif defined(STM32F4xx)
#ifndef F_CPU
#define F_CPU 180000000UL
#endif
#define HAL_RAM_SIZE      (128UL * 1024UL)
#define HAL_FLASH_SIZE    (256UL * 1024UL)  // STM32F446RC
#endif

// RAM boundaries (used by the D2 memory dump D-code)
#define RAMSTART          0x20000000UL
#define RAMEND            (0x20000000UL + HAL_RAM_SIZE - 1)

#define HAL_PRIO_STEPPER  0
#define HAL_PRIO_COMM     1
#define HAL_PRIO_SOFTPWM  2
#define HAL_PRIO_EXTI     2
#define HAL_PRIO_TEMPMGR  3
#define HAL_PRIO_LCD      3

//! Stepper timer. Runs at 2 MHz like Timer1 with the /8 prescaler on a 16 MHz AVR,
//! so the stepper/planner timing math and speed_lookuptable work unchanged.
//! *_CLK: input clock of the timer (F4: the APB1 timers run at 90 MHz).
#if defined(STM32G0xx)
#define STEP_TIMER        TIM17
#define STEP_TIMER_IRQn   TIM17_FDCAN_IT1_IRQn
#define STEP_TIMER_CLK    F_CPU
#define STEP_TIMER_CLK_ENABLE() __HAL_RCC_TIM17_CLK_ENABLE()
#define SOFTPWM_TIMER     TIM16
#define SOFTPWM_TIMER_CLK F_CPU
#define TEMPMGR_TIMER     TIM15
#define TEMPMGR_TIMER_CLK F_CPU
#elif defined(STM32F4xx)
#define STEP_TIMER        TIM7
#define STEP_TIMER_IRQn   TIM7_IRQn
#define STEP_TIMER_CLK    (F_CPU / 2)
#define STEP_TIMER_CLK_ENABLE() __HAL_RCC_TIM7_CLK_ENABLE()
#define SOFTPWM_TIMER     TIM13
#define SOFTPWM_TIMER_CLK (F_CPU / 2)
#define TEMPMGR_TIMER     TIM14
#define TEMPMGR_TIMER_CLK (F_CPU / 2)
#endif
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

//! Motion engine step timer (STEP_TIMER) at 2 MHz, update interrupt when CNT reaches ARR.
//! The handler is installed directly in the (RAM) vector table: it runs at every step,
//! the generic HardwareTimer dispatch would cost ~200 cycles each time.
void hal_motion_timer_init(hal_isr_t isr);
//! Start counting from 0, first interrupt after @p period ticks
void hal_motion_timer_start(uint32_t period);
void hal_motion_timer_stop(void);
void hal_motion_timer_irq_enable(bool enable);

//! Background transfer of the ST7920 display (TIM7, 25 kHz, lowest priority; STM32G0 only).
//! kick() starts the timer if it is stopped, the ISR stops it when it has nothing to send.
void hal_lcd_timer_init(hal_isr_t isr);
void hal_lcd_timer_kick(void);
void hal_lcd_timer_stop(void);
//! Called at the end of every soft PWM tick (~976 Hz, priority 2): motion engine producer
extern hal_isr_t hal_motion_tick;

//! Install an interrupt handler in the RAM copy of the vector table
void hal_set_irq_handler(IRQn_Type irq, hal_isr_t handler);

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
