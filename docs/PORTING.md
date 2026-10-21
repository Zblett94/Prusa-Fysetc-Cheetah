# How the port works

The goal is to run the **unchanged printer logic** of Prusa-Firmware 3.14.1 for the
Original Prusa i3 MK3S (G-code handling, planner, stepper ISR, mesh bed leveling,
XYZ calibration, thermal model, menus, power panic, crash detection, MMU support,
filament sensor, self test, ...) on the STM32G0B1 of the BigTreeTech SKR Mini E3 V3.

Instead of rewriting these parts on top of another firmware, the AVR (ATmega2560)
peripherals the Prusa code uses are emulated by a hardware abstraction layer in
`Firmware/hal_stm32`. Code outside the HAL is only changed where it accessed AVR
registers directly; those places are wrapped in `#ifdef __AVR__ ... #else ... #endif`
so the original code stays visible next to the port. The first commit of the
repository is the unmodified Prusa source, so `git diff <first commit> -- Firmware`
shows every change.

## Peripheral mapping

| ATmega2560 (EINSy) | STM32G0B1 (SKR Mini E3 V3) | Where |
|---|---|---|
| `PORTx`/`PINx`/`DDRx` (`fastio.h`) | GPIO BSRR/IDR/MODER, constexpr pin tables. Writing an input pin still switches its pull-up, as on the AVR | `hal_stm32/fastio_stm32.h` |
| Timer1 CTC, /8 = 2 MHz, `OCR1A`/`TCNT1` | TIM17 at 2 MHz, `OCR1A` = ARR, `TCNT1` = CNT, update interrupt. Same tick, so `speed_lookuptable` and all step timing math are unchanged | `hal.cpp`, `stepper.cpp` |
| Timer2 COMPB, 976.5 Hz (soft PWM, buttons, babystepping, fan tach) | TIM16 at 976.5625 Hz | `hal.cpp`, `temperature.cpp` |
| Timer5 COMPA, 3.7 Hz (temperature manager, PID, thermal model) | TIM15, 0.27 s | `hal.cpp`, `temperature.cpp` |
| Timer0 fast PWM 62.5 kHz / 7.8 kHz (quiet heatbed PWM automaton) | TIM1 CH2 on PC9, same frequencies, same state machine | `heatbed_pwm.cpp` |
| Timer4 (beeper tone, hotend fan PWM) | Arduino `tone()` on TIM4; hotend fan soft PWM on the 976 Hz tick | `system_timer_stm32.cpp` |
| Timer2 overflow (`millis`) | SysTick | `system_timer.h` |
| ADC, 10 bit, AVcc reference, ADC ISR, 16x oversampling | ADC1, 12 bit >> 2, EOC interrupt, 16x oversampling. The thermistor dividers are ratiometric, so the AVR raw value tables apply | `adc.cpp` |
| Ambient thermistor of the EINSy | STM32 internal temperature sensor (with VREFINT compensation), converted into the raw value of the EINSy thermistor table, so the min/max checks and the thermal model work unchanged | `temperature.cpp` |
| 4 KiB internal EEPROM | 4 KiB AT24C32 on the board (bit-banged I2C), mirrored in RAM, written through page by page. Same addresses | `eeprom_i2c.cpp` |
| TMC2130 over SPI | TMC2209 over the single wire UART (USART4, polled). Register translation, see below | `tmc2209_uart.cpp`, `tmc2130.cpp` |
| USART0 (to the 32u2 USB bridge) | native USB CDC | `MarlinSerial.cpp` |
| USART2 (MMU) | USART2 on PA2/PA3 | `mmu2_serial.cpp` |
| SPI (SD card on the LCD) | SPI1, on-board micro-SD | `Sd2Card.cpp` |
| INT4 (power panic), INT6/INT7 (fan tach) | EXTI lines, the INT7 edge toggling is emulated | `power_panic.cpp`, `fancheck.cpp` |
| WDT | IWDG. "Disabling" it keeps it fed from SysTick (the IWDG can not be stopped) | `hal.cpp` |
| avr-libc (`pgmspace`, `eeprom`, `wdt`, `atomic`, `delay`, `crc16`, `%S`) | compatibility headers in `hal_stm32/include`, `%S` replaced by `%s` | |

### Interrupt priorities

The AVR code re-enables interrupts inside some ISRs (`NONATOMIC_BLOCK`) so the stepper
interrupt can preempt them. On the Cortex-M0+ this is done with NVIC priorities:

| Priority | Interrupts |
|---|---|
| 0 | stepper (TIM17), SysTick |
| 1 | USB, MMU UART |
| 2 | soft PWM tick, heatbed PWM, ADC, power panic, fan tach |
| 3 | temperature manager |

### Timing constants

Constants that the Prusa code derives from `F_CPU` (PID sample time, planner
acceleration rate, speed table selection) use `TIMING_F_CPU` = 16 MHz, because the
timers emulate those of the 16 MHz AVR.

### 32 bit related changes

- `int` is 32 bit: signed 16 bit values read with `eeprom_read_word()` (Live Z /
  babystep, PINDA temperature compensation) are cast to `int16_t` first.
- `char` is unsigned on ARM by default: the firmware is built with `-fsigned-char`.
- Structures describing the EEPROM layout (`Sheet`, `Sheets`) are packed; `M500_conf`
  is kept naturally aligned (216 instead of 209 bytes, still inside its EEPROM area)
  because the Cortex-M0+ cannot access unaligned floats.
- Flash pointers are 32 bit: the kill message pointer, stored as 16 bits in the
  EEPROM on the AVR, is also kept in a backup register across the reset.
- `menu_data` is word aligned.

## Languages

The EINSy keeps one secondary language in the ATmega flash and the others in the
external SPI flash; selecting one of those makes the bootloader copy it into the
ATmega flash. The STM32G0B1 has enough flash for all of them: `_SEC_LANG`
(`LANG_SIZE_RESERVED` = 192 KiB, `config.h`) holds all language tables one after the
other, and every language is selected directly from the menu (`language.c`).

The tables have the Prusa format and are built by the Prusa tools. After linking,
`buildroot/scripts/lang.py`

1. collects the translatable strings (`_I()`/`ISTR()`, section `loc_pri`) and
   numbers them in address order, like `lang/lang-map.py`,
2. builds one table per `lang/po/Firmware_xx.po` with `lang/lang-build.py`,
3. writes the string IDs, the signature `_PRI_LANG_SIGNATURE` and the tables into
   `firmware.bin`.

The ELF file is not patched; it runs in English only.

## Emulator tests

`pio run -e renode_test` builds a test variant (`-DEMULATION`) for the
[Renode](https://renode.io) emulator: the host serial is USART2, the EEPROM is preset
like a calibrated printer, the heaters, thermistors (with the response lag of the
E3D V6 that the thermal model expects) and the hotend fan tachometer are simulated,
and the LCD contents are mirrored into RAM. `test/renode/run.py`
boots it, sends G-code and checks the serial output and the LCD;
`test/renode/smoke_test.sh` checks start-up, M115, M503 and moves,
`test/renode/heat_test.sh` heating and temperature regulation,
`test/renode/lang_test.sh` the language menu and a German message. The emulator does not model the TMC2209 drivers, EXTI and the ADC, so
these tests cover the firmware logic, not the hardware layer.

## TMC2209 instead of TMC2130

The register map of the TMC2209 is close to the TMC2130 one. `tmc2130.cpp` keeps its
interface and translates:

- **Currents.** The SKR uses 0.11 Ω sense resistors (EINSy: 0.22 Ω). The Prusa current
  values (0-63, M911/M912/M913) keep their meaning; they are converted so that the
  motor current is the same as on the EINSy.
- **StallGuard.** StallGuard4 (SGTHRS/SG_RESULT) replaces StallGuard2 (COOLCONF SGT).
  StallGuard4 only works in stealthChop, so the axes that need stall detection run in
  stealthChop where the TMC2130 used spreadCycle: X/Y in the Normal mode (crash
  detection) and during homing, Z always. The extruder keeps spreadCycle. Define
  `TMC2209_NORMAL_MODE_SPREADCYCLE` to keep spreadCycle on X/Y in the Normal mode
  (crash detection is then not available).
  The thresholds are `TMC2209_SGTHRS_*` in the variant file, `M916 X<n> Y<n>`
  changes them at run time. **Higher values are more sensitive** (the opposite of the
  TMC2130).
- **DIAG** is active high on the TMC2209. The DIAG pins are flagged with
  `FASTIO_INVERTED` so the Prusa code sees the TMC2130 (active low) levels.
- **Not available:** the programmable microstep table (linearity correction,
  `TMC2130_LINEARITY_CORRECTION`), XDIRECT, VDCMIN, THIGH.
