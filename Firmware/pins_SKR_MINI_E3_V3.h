/*****************************************************************
* BigTreeTech SKR Mini E3 V3.0 (STM32G0B1RET6) Pin Assignments
* wired as an Original Prusa i3 MK3S (see docs/WIRING.md)
******************************************************************/

#define ELECTRONICS "SKR_MINI_E3_V3"

#define KNOWN_BOARD
#ifndef STM32G0B1xx
  #error Oops!  This pin file is for the STM32G0B1 based BigTreeTech SKR Mini E3 V3.0.
#endif

#include <pins_arduino.h>
#include "hal_stm32/hal.h"

#define TMC2130              // the Prusa TMC2130 code paths are used, the driver layer talks to TMC2209s
#define TMC2209              // ... over the single wire UART (USART4, PC10/PC11)
#define UVLO_SUPPORT
#define PINDA_THERMISTOR     // PINDA v2 thermistor on the SERVO header (PA1), see docs/WIRING.md
#define AMBIENT_THERMISTOR   // the STM32 internal temperature sensor replaces the EINSy board thermistor
//#define XFLASH             // no external SPI flash: no languages, no crash dumps
//#define BOOTAPP            // the BTT SD card bootloader is used instead
#define WATCHDOG_SOFT_RESET_VALUE WDTO_15MS

// TMC2209 UART slave addresses (fixed by the MS1/MS2 straps of the board)
#define X_TMC2209_ADDR       0
#define Y_TMC2209_ADDR       2
#define Z_TMC2209_ADDR       1
#define E0_TMC2209_ADDR      3
#define TMC2209_RSENSE       0.11f // ohm
#define TMC_UART_TX_PIN      PC10
#define TMC_UART_RX_PIN      PC11

// The DIAG outputs of the drivers are connected to the X/Y/Z-STOP inputs by
// the four "DIAG" jumpers next to the drivers (X, Y and Z must be installed,
// E0 must be removed because E0-STOP carries the filament sensor).
// TMC2209 DIAG is active high, the Prusa code expects the active low TMC2130 DIAG.
#define X_TMC2130_DIAG       (PC0 | FASTIO_INVERTED)  // X-STOP
#define Y_TMC2130_DIAG       (PC1 | FASTIO_INVERTED)  // Y-STOP
#define Z_TMC2130_DIAG       (PC2 | FASTIO_INVERTED)  // Z-STOP
#define E0_TMC2130_DIAG      -1

#define X_STEP_PIN           PB13
#define X_DIR_PIN            PB12
#define X_ENABLE_PIN         PB14
#define X_MIN_PIN            -1
#define X_MAX_PIN            X_TMC2130_DIAG
#define X_MS1_PIN            -1
#define X_MS2_PIN            -1

#define Y_STEP_PIN           PB10
#define Y_DIR_PIN            PB2
#define Y_ENABLE_PIN         PB11
#define Y_MIN_PIN            -1
#define Y_MAX_PIN            Y_TMC2130_DIAG
#define Y_MS1_PIN            -1
#define Y_MS2_PIN            -1

#define Z_STEP_PIN           PB0
#define Z_DIR_PIN            PC5
#define Z_ENABLE_PIN         PB1
#define Z_MIN_PIN            PC14   // PROBE header: P.I.N.D.A.
#define Z_MAX_PIN            -1
#define Z_MS1_PIN            -1
#define Z_MS2_PIN            -1

#define E0_STEP_PIN          PB3
#define E0_DIR_PIN           PB4
#define E0_ENABLE_PIN        PD1
#define E0_MS1_PIN           -1
#define E0_MS2_PIN           -1

#define HEATER_BED_PIN       PC9    // "HB" (TIM1_CH2 fast PWM, see heatbed_pwm.cpp)
#define HEATER_0_PIN         PC8    // "HE"
#define HEATER_1_PIN         -1
#define HEATER_2_PIN         -1

// Analog inputs are given as ADC1 channel numbers, like the AVR ADC channels
#define TEMP_0_PIN           0      // PA0  "TH0"  hotend thermistor
#define TEMP_BED_PIN         17     // PC4  "TB"   bed thermistor
#define TEMP_PINDA_PIN       1      // PA1  "SERVO" PINDA thermistor (needs a 4.7k pull-up to 3.3V)
#define TEMP_1_PIN           -1
#define TEMP_2_PIN           -1
//#define VOLT_PWR_PIN       not available
//#define VOLT_BED_PIN       not available
#define TEMP_AMBIENT_PIN     12     // ADC1_IN12: STM32 internal temperature sensor (see temperature.cpp)
#define VREFINT_ADC_CHANNEL  13     // ADC1_IN13: internal reference, to compensate the supply voltage

//! ADC1 channel -> GPIO (NUM_DIGITAL_PINS for the internal channels)
static inline uint32_t adc_channel_pin(uint8_t ch) {
	switch (ch) {
	case 0: return PA0;
	case 1: return PA1;
	case 17: return PC4;
	}
	return NUM_DIGITAL_PINS;
}
#undef analogInputToDigitalPin
#define analogInputToDigitalPin(p) (adc_channel_pin(p))

#define TEMP_TIM 5

#define SDPOWER              -1
#define SDSS                 PA4    // on-board micro SD (SPI1)
#define SD_SCK_PIN           PA5
#define SD_MISO_PIN          PA6
#define SD_MOSI_PIN          PA7
#define SDCARDDETECT         PC3
#define LED_PIN              PD8

#define FAN_PIN              PC6    // "FAN0": print fan (5V, set the fan voltage jumper accordingly)
#define FAN_1_PIN            -1
#define PS_ON_PIN            -1
#define KILL_PIN             -1
#define SUICIDE_PIN          -1

// Original Prusa i3 MK3S LCD (20x4 HD44780 + encoder + beeper), see docs/WIRING.md
//#define LCD_BL_PIN         not connected
#define BEEPER               PA8    // "NEOPIXEL" header
#define LCD_PINS_RS          PB8    // EXP1-7
#define LCD_PINS_ENABLE      PD6    // EXP1-8
#define LCD_PINS_D4          PB9    // EXP1-6
#define LCD_PINS_D5          PA10   // EXP1-5
#define LCD_PINS_D6          PA9    // EXP1-3
#define LCD_PINS_D7          PB5    // EXP1-1

#define BTN_EN1              PD9    // "SPI1" header pin 3 (CS)
#define BTN_EN2              PC13   // "PS-ON" header (PC13 is an input-only grade pin, fine for the encoder)
#define BTN_ENC              PA15   // EXP1-2

// Fan tachometers. The SWD header pins are used (no other free pins left),
// so a debugger can not be connected while the tach wires are attached.
#define TACH_0               PA14   // "SWD" header SWCLK - hotend fan tach
#define TACH_1               PA13   // "SWD" header SWDIO - print fan tach

#define IR_SENSOR_PIN        PC15   // "E0-STOP": MK3S IR filament sensor
#define UVLO_PIN             PC12   // "PWR-DET": power panic signal from the PSU
//#define MMU_RST_PIN        the MMU is reset over its serial protocol
#define MMU_SERIAL_RX_PIN    PA3_ALT1 // "TFT" header (USART2)
#define MMU_SERIAL_TX_PIN    PA2_ALT1

#define EXTRUDER_0_AUTO_FAN_PIN PC7 // "FAN1": hotend fan (5V)

// pins which M42/M226 must not touch (STM32 core digital pin numbers)
#define _SKR_PIN(p) fastio::digital_index(p)
#define SENSITIVE_PINS { \
	_SKR_PIN(X_STEP_PIN), _SKR_PIN(X_DIR_PIN), _SKR_PIN(X_ENABLE_PIN), _SKR_PIN(X_TMC2130_DIAG), \
	_SKR_PIN(Y_STEP_PIN), _SKR_PIN(Y_DIR_PIN), _SKR_PIN(Y_ENABLE_PIN), _SKR_PIN(Y_TMC2130_DIAG), \
	_SKR_PIN(Z_STEP_PIN), _SKR_PIN(Z_DIR_PIN), _SKR_PIN(Z_ENABLE_PIN), _SKR_PIN(Z_MIN_PIN), _SKR_PIN(Z_TMC2130_DIAG), \
	_SKR_PIN(E0_STEP_PIN), _SKR_PIN(E0_DIR_PIN), _SKR_PIN(E0_ENABLE_PIN), \
	_SKR_PIN(HEATER_0_PIN), _SKR_PIN(HEATER_BED_PIN), _SKR_PIN(FAN_PIN), _SKR_PIN(EXTRUDER_0_AUTO_FAN_PIN), \
	_SKR_PIN(PA0), _SKR_PIN(PA1), _SKR_PIN(PC4), \
	_SKR_PIN(TMC_UART_TX_PIN), _SKR_PIN(TMC_UART_RX_PIN), _SKR_PIN(UVLO_PIN), \
	_SKR_PIN(PA11), _SKR_PIN(PA12) /* USB */ }
