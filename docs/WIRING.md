# Wiring an Original Prusa i3 MK3S to the SKR Mini E3 V3.0

The pin map below is the one compiled into the firmware
(`Firmware/pins_SKR_MINI_E3_V3.h`). Change it there if you wire things differently.

> **Signal levels.** The STM32G0B1 is a 3.3 V part. Several MK3S parts (P.I.N.D.A.,
> IR filament sensor, LCD board, MMU, PSU power-panic line) were designed for the 5 V
> EINSy. Before connecting a 5 V signal to an STM32 input, check that the pin is
> 5 V tolerant ("FT" in the STM32G0B1 datasheet) or put a resistor divider
> (for example 10 kΩ series / 20 kΩ to GND) in the line. Measure first.

## Board jumpers

| Jumper | Setting | Why |
|---|---|---|
| DIAG X, Y, Z (next to the drivers) | **installed** | StallGuard output of the TMC2209 to X-STOP/Y-STOP/Z-STOP: sensorless homing, crash detection, Z calibration |
| DIAG E0 | **removed** | E0-STOP is used by the filament sensor |
| Fan voltage | **5 V** (or supply the fans from a separate 5 V source) | the MK3S hotend fan (Noctua) and print fan (blower) are 5 V fans |

## Motors, heaters, thermistors

| MK3S part | SKR header | MCU pin | Notes |
|---|---|---|---|
| X motor | XM | PB13/PB12/PB14 (step/dir/en) | TMC2209, UART address 0 |
| Y motor | YM | PB10/PB2/PB11 | TMC2209, UART address 2 |
| Z motors (both, in parallel as on the EINSy) | ZAM / ZBM | PB0/PC5/PB1 | TMC2209, UART address 1 |
| Extruder motor | EM | PB3/PB4/PD1 | TMC2209, UART address 3 |
| Hotend heater (40 W) | HE | PC8 | |
| Heatbed MK52 | HB | PC9 | 24 V, ~9 A. An external MOSFET module is recommended |
| Hotend thermistor (Semitec 104GT-2) | TH0 | PA0 | 4.7 kΩ pull-up on the board |
| Heatbed thermistor | TB | PC4 | 4.7 kΩ pull-up on the board |

If an axis moves the wrong way, flip `INVERT_?_DIR` in
`Firmware/variants/MK3S-SKR_MINI_E3_V3.h` (the coil order of the motor connectors
may differ from the EINSy).

## P.I.N.D.A., filament sensor, fans, power panic

| MK3S part | SKR header | MCU pin | Notes |
|---|---|---|---|
| P.I.N.D.A. signal | PROBE (signal pin) | PC14 | 5 V supply from the PROBE header. Check the output level (see above) |
| P.I.N.D.A. thermistor (PINDA v2) | SERVO (signal pin) | PA1 | **add a 4.7 kΩ resistor from PA1 to 3.3 V**; other wire to GND. Without a thermistor (SuperPINDA) remove `PINDA_THERMISTOR` from the pin file |
| IR filament sensor output | E0-STOP | PC15 | sensor supply 5 V |
| Print fan (5 V blower) | FAN0 | PC6 | |
| Hotend fan (5 V Noctua) | FAN1 | PC7 | |
| Print fan tachometer | SWD header, SWDIO | PA13 | internal pull-up. No debugger can be used while connected |
| Hotend fan tachometer | SWD header, SWCLK | PA14 | internal pull-up |
| PSU power-panic signal | PWR-DET | PC12 | active low, internal pull-up (unconnected = power good) |
| MMU2S/MMU3 serial | TFT header | PA2 = TX → MMU RX, PA3 = RX ← MMU TX | 115200 Bd, the MMU is reset over the protocol (no reset wire) |

## LCD (original MK3S 20x4 LCD with encoder)

The MK3S LCD board uses two 10-pin cables (EXP1, EXP2) like the RepRapDiscount Smart
Controller. The SKR Mini E3 V3 has one EXP1 header, so a custom harness is needed.
The micro-SD slot of the LCD is not used (use the slot on the SKR).

| LCD signal | SKR header / pin | MCU pin |
|---|---|---|
| LCD_RS | EXP1-7 | PB8 |
| LCD_EN | EXP1-8 | PD6 |
| LCD_D4 | EXP1-6 | PB9 |
| LCD_D5 | EXP1-5 | PA10 |
| LCD_D6 | EXP1-3 | PA9 |
| LCD_D7 | EXP1-1 | PB5 |
| BTN_ENC (encoder push) | EXP1-2 | PA15 |
| BTN_EN1 (encoder A) | SPI1 header pin 3 (CS) | PD9 |
| BTN_EN2 (encoder B) | PS-ON header signal | PC13 |
| BEEPER | NEOPIXEL header signal | PA8 |
| 5 V / GND | EXP1-10 / EXP1-9 | |

EXP1 pin numbers follow the BigTreeTech numbering used by Marlin
(`pins_BTT_SKR_MINI_E3_V3_0.h`): 1 = PB5, 2 = PA15, 3 = PA9, 4 = RESET, 5 = PA10,
6 = PB9, 7 = PB8, 8 = PD6, 9 = GND, 10 = 5 V. Check the notch orientation of your
cable against the board silkscreen.

## Pins that are used by the firmware internally

| Function | MCU pins |
|---|---|
| USB (host serial) | PA11/PA12 |
| TMC2209 UART | PC10 (TX) / PC11 (RX) |
| AT24C32 EEPROM (settings) | PB6 (SCL) / PB7 (SDA) |
| micro-SD card | PA4 (CS), PA5/PA6/PA7 (SPI1), PC3 (detect) |
| status LED | PD8 |
