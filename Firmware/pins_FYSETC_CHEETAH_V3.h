/*****************************************************************
* FYSETC Cheetah V3.0 (STM32F446RCT6) Pin Assignments
* wired as an Original Prusa i3 MK3S with its original LCD (see docs/WIRING_CHEETAH.md)
*
* From the FYSETC schematic (github.com/FYSETC/Cheetah_V3.0, Hardware/CHEETAH V3.0_SCH.pdf).
* All 51 GPIOs of the STM32F446RC are used by the board; the only pins left for the
* printer are the two of USART1 on the Raspberry Pi header: they carry the MMU serial
* port. No power panic, no fan tachometers.
******************************************************************/

#define ELECTRONICS "FYSETC_CHEETAH_V3"

#define KNOWN_BOARD
#ifndef STM32F446xx
  #error Oops!  This pin file is for the STM32F446 based FYSETC Cheetah V3.0.
#endif

#include <pins_arduino.h>
#include "hal_stm32/hal.h"

#define TMC2130              // the Prusa TMC2130 code paths are used, the driver layer talks to TMC2209s
#define TMC2209              // ... over the single wire UART (PB3, bit-banged)
#define PINDA_THERMISTOR     // PINDA v2 thermistor on "T2" (PB1), see docs/WIRING_CHEETAH.md
#define AMBIENT_THERMISTOR   // the STM32 internal temperature sensor replaces the EINSy board thermistor
#define EEPROM_FLASH_EMULATION // no EEPROM chip: the settings are stored in flash sectors 1 and 2
//#define XFLASH             // no external SPI flash: no languages, no crash dumps
//#define BOOTAPP            // flashed over USB DFU (BOOT0 jumper)
#define WATCHDOG_SOFT_RESET_VALUE WDTO_15MS

// TMC2209 UART slave addresses: set by 0 ohm straps; the firmware detects the mapping at
// start-up (TMC2209_ADDR_DETECT, see tmc2130.cpp), these are the defaults
#define TMC2209_ADDR_DETECT
#define X_TMC2209_ADDR       0
#define Y_TMC2209_ADDR       2
#define Z_TMC2209_ADDR       1
#define E0_TMC2209_ADDR      3
#define TMC2209_RSENSE       0.22f // ohm (same as the EINSy)
#define TMC_UART_PIN         PB3   // shared PDN_UART line of the four drivers

// The DIAG outputs of the drivers reach X-MIN/Y-MIN/Z-MIN through the jumpers JP3/JP4/JP5
// (X, Y and Z must be installed; nothing may be plugged into X-min/Y-min/Z-min then).
// TMC2209 DIAG is active high, the Prusa code expects the active low TMC2130 DIAG.
#define X_TMC2130_DIAG       (PA2 | FASTIO_INVERTED)  // X-MIN
#define Y_TMC2130_DIAG       (PA3 | FASTIO_INVERTED)  // Y-MIN
#define Z_TMC2130_DIAG       (PC4 | FASTIO_INVERTED)  // Z-MIN
#define E0_TMC2130_DIAG      -1

#define X_STEP_PIN           PC0
#define X_DIR_PIN            PC1
#define X_ENABLE_PIN         PC3
#define X_MIN_PIN            X_TMC2130_DIAG // homeaxis() runs only if the pin of the home side is defined (the EINSy uses a spare pin); the stop itself is the DIAG stall
#define X_MAX_PIN            X_TMC2130_DIAG
#define X_MS1_PIN            -1
#define X_MS2_PIN            -1

#define Y_STEP_PIN           PC14
#define Y_DIR_PIN            PC13
#define Y_ENABLE_PIN         PC15
#define Y_MIN_PIN            Y_TMC2130_DIAG // see X_MIN_PIN
#define Y_MAX_PIN            Y_TMC2130_DIAG
#define Y_MS1_PIN            -1
#define Y_MS2_PIN            -1

#define Z_STEP_PIN           PB4
#define Z_DIR_PIN            PB5
#define Z_ENABLE_PIN         PC2
#define Z_MIN_PIN            PA0    // "Probe" header (BAT85 level shifter on the board): P.I.N.D.A.
#define Z_MAX_PIN            Z_TMC2130_DIAG // stall at the top: Calibrate Z (stepper.cpp checks Z+ only if defined)
#define Z_MS1_PIN            -1
#define Z_MS2_PIN            -1

#define E0_STEP_PIN          PB2
#define E0_DIR_PIN           PA15
#define E0_ENABLE_PIN        PD2
#define E0_MS1_PIN           -1
#define E0_MS2_PIN           -1

#define HEATER_BED_PIN       PC8    // "Heated-Bed" (TIM3_CH3 fast PWM, see heatbed_pwm.cpp)
#define HEATER_0_PIN         PC7    // "E0" heater
#define HEATER_1_PIN         -1
#define HEATER_2_PIN         -1

// Analog inputs are given as ADC1 channel numbers, like the AVR ADC channels
#define TEMP_0_PIN           15     // PC5  "T0"  hotend thermistor
#define TEMP_BED_PIN         8      // PB0  "T1"  bed thermistor
#define TEMP_PINDA_PIN       9      // PB1  "T2"  PINDA thermistor (4.7k pull-up on the board)
#define TEMP_1_PIN           -1
#define TEMP_2_PIN           -1
//#define VOLT_PWR_PIN       not available
//#define VOLT_BED_PIN       not available
#define TEMP_AMBIENT_PIN     18     // ADC1_IN18: STM32 internal temperature sensor (see temperature.cpp)
#define VREFINT_ADC_CHANNEL  17     // ADC1_IN17: internal reference, to compensate the supply voltage

//! ADC1 channel -> GPIO (NUM_DIGITAL_PINS for the internal channels)
static inline uint32_t adc_channel_pin(uint8_t ch) {
	switch (ch) {
	case 8: return PB0;
	case 9: return PB1;
	case 15: return PC5;
	}
	return NUM_DIGITAL_PINS;
}
#undef analogInputToDigitalPin
#define analogInputToDigitalPin(p) (adc_channel_pin(p))

#define TEMP_TIM 5

// micro SD: the on-board TF slot and the slot of the MK3S LCD (EXP2) share SPI1, the chip
// select and the card detect. Use one of them.
#define SDPOWER              -1
#define SDSS                 PA4
#define SD_SCK_PIN           PA5
#define SD_MISO_PIN          PA6
#define SD_MOSI_PIN          PA7
#define SDCARDDETECT         PC12
#define LED_PIN              -1

#define FAN_PIN              PA14   // "FAN0": print fan (fan voltage jumper JP2 on 5V). PA14 = SWCLK: no debugger
#define FAN_1_PIN            -1
#define PS_ON_PIN            -1
#define KILL_PIN             -1
#define SUICIDE_PIN          -1

// Original Prusa i3 MK3S LCD (20x4 HD44780 + encoder + beeper + SD) on EXP1/EXP2,
// which have the RepRapDiscount Smart Controller layout of the LCD cables.
// Pin numbers as in the FYSETC schematic and Marlin's pins_FYSETC_CHEETAH_V30.h:
// EXP1-1 = 5V, EXP1-2 = GND, EXP2-1 = KILL, EXP2-2 = GND.
//#define LCD_BL_PIN         not connected
#define BEEPER               PB10   // EXP1-10
#define BTN_ENC              PC6    // EXP1-9
#define LCD_PINS_ENABLE      PB15   // EXP1-8
#define LCD_PINS_RS          PB12   // EXP1-7
#define LCD_PINS_D4          PB13   // EXP1-6
#define LCD_PINS_D5          PB14   // EXP1-5
#define LCD_PINS_D6          PB6    // EXP1-4
#define LCD_PINS_D7          PB7    // EXP1-3
#define BTN_EN1              PC10   // EXP2-8
#define BTN_EN2              PC11   // EXP2-6
// EXP2: 10 = MISO (PA6), 9 = SCK (PA5), 7 = SS (PA4), 5 = MOSI (PA7), 4 = CD (PC12), 3 = RESET

#define IR_SENSOR_PIN        PA1    // "FIL-DET": MK3S IR filament sensor

// MMU2S/MMU3 on USART1 of the Raspberry Pi header (TXD = PA9, RXD = PA10)
#define MMU_SERIAL_RX_PIN    PA10
#define MMU_SERIAL_TX_PIN    PA9
//#define MMU_RST_PIN        the MMU is reset over its serial protocol

#define EXTRUDER_0_AUTO_FAN_PIN PA13 // "FAN1": hotend fan (fan voltage jumper JP1 on 5V). PA13 = SWDIO

// pins which M42/M226 must not touch (STM32 core digital pin numbers)
#define _SKR_PIN(p) fastio::digital_index(p)
#define SENSITIVE_PINS { \
	_SKR_PIN(X_STEP_PIN), _SKR_PIN(X_DIR_PIN), _SKR_PIN(X_ENABLE_PIN), _SKR_PIN(X_TMC2130_DIAG), \
	_SKR_PIN(Y_STEP_PIN), _SKR_PIN(Y_DIR_PIN), _SKR_PIN(Y_ENABLE_PIN), _SKR_PIN(Y_TMC2130_DIAG), \
	_SKR_PIN(Z_STEP_PIN), _SKR_PIN(Z_DIR_PIN), _SKR_PIN(Z_ENABLE_PIN), _SKR_PIN(Z_MIN_PIN), _SKR_PIN(Z_TMC2130_DIAG), \
	_SKR_PIN(E0_STEP_PIN), _SKR_PIN(E0_DIR_PIN), _SKR_PIN(E0_ENABLE_PIN), \
	_SKR_PIN(HEATER_0_PIN), _SKR_PIN(HEATER_BED_PIN), _SKR_PIN(FAN_PIN), _SKR_PIN(EXTRUDER_0_AUTO_FAN_PIN), \
	_SKR_PIN(PB0), _SKR_PIN(PB1), _SKR_PIN(PC5), \
	_SKR_PIN(TMC_UART_PIN), _SKR_PIN(PA9), _SKR_PIN(PA10), \
	_SKR_PIN(PA11), _SKR_PIN(PA12) /* USB */ }
