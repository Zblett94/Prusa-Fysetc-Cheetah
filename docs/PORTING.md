# How the port works

The goal is to run the **unchanged printer logic** of Prusa-Firmware 3.14.1 for the
Original Prusa i3 MK3S (G-code handling, planner, mesh bed leveling, XYZ calibration,
thermal model, menus, power panic, crash detection, MMU support, filament sensor, self
test, ...) on the STM32G0B1 of the BigTreeTech SKR Mini E3 V3. The stepper interrupt
is replaced by a motion engine with the input shaper and pressure advance of the MK3.5
(see [Motion engine](#motion-engine-input-shaper-and-pressure-advance)).

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
| Timer1 CTC, /8 = 2 MHz (stepper interrupt) | TIM17 at 2 MHz, update interrupt, executes the step events of the motion engine; its handler is installed in a RAM copy of the vector table | `hal.cpp`, `stepper.cpp`, `motion/` |
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
| HD44780 20x4 LCD, 4 bit parallel | 480x320 color TFT (ST7796S, SPI2 + DMA) emulating the HD44780 with its own layout; or the Creality Ender-3 display (`-DLCD_ENDER3`, ST7920, TIM7) or the MK3S LCD (`-DLCD_MK3S`) | `lcd_tft.cpp`, `lcd_st7920.cpp`, `lcd.cpp` |
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
| 2 | soft PWM tick (with the motion engine producer), heatbed PWM, ADC, power panic, fan tach |
| 3 | temperature manager, display transfer (TFT: DMA1 channel 1 + TIM7 as software interrupt; Ender-3: TIM7) |

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

## Motion engine (input shaper and pressure advance)

The MK3.5 (Prusa-Firmware-Buddy) computes the time of every step with floating point
math ("PreciseStepping"). The Cortex-M0+ has no FPU: that way one step costs ~2500
cycles, too much for the 20-40k steps/s of the MK3.5 speeds. The engine in
`Firmware/motion` keeps the planner of 3.14 and the shaper/pressure advance math of
Buddy (both based on Klipper), and changes how steps are generated:

1. **Segments.** Each planner block becomes up to three constant acceleration
   segments along its path (`motion_core.cpp`, `add_block`). The blocks stay in the
   planner queue (`BLOCK_BUFFER_SIZE` 64) until the shaped motion has left them, so
   `current_block`, `blocks_queued()`, the SD position for power panic and the
   crash recovery work as before; the planner re-plans only the blocks after
   `block_buffer_motion` (the ones not yet taken by the engine).
2. **Sampling.** Every millisecond the position of each motor is sampled:
   X and Y through the input shaper, x_s(t) = Σ aᵢ·x(t − tᵢ) (MZV/ZV/ZVD/EI... of
   Buddy, pulses centered around their weighted average so that shaped and unshaped
   axes stay in sync), E with pressure advance e(t) + K·v_e(t), smoothed by a
   triangular window (`M572 W`, 40 ms by default; two box filters with integer
   running sums). Inside a segment a motor position is a quadratic function of time,
   so it is advanced by forward differencing in 32.32 fixed point; floating point is
   only used when a motor enters a new segment.
3. **Steps.** Between two samples a motor moves linearly, so its steps are evenly
   spaced: one integer division per motor and sample, then additions. The steps of all
   motors are merged into one time ordered event queue (with markers that discard the
   finished planner blocks and apply E resets).
4. **Step interrupt.** TIM17 executes the events: direction and step pins (double edge
   stepping), `count_position`, endstops (an endstop hit stops the motion and discards
   the block, like the AVR interrupt did).

The producer (steps 1-3) runs in the 1 kHz soft PWM interrupt and stays 12 ms ahead
of the step interrupt. Compared to exact step times the position error is below 0.1
step (`test/motion`); the position after a move is exact.

CPU load measured in the emulator (one instruction per clock) at the MK3.5 limits,
300 mm/s and 7000 mm/s² diagonal with input shaping: step interrupt 11 %, producer
14 %. The real Cortex-M0+ needs more cycles per instruction (flash wait states), so
expect roughly a third of the CPU at full speed; `M593` prints the measured load.

G-codes: `M593` (input shaper, as on the MK3.5), `M74` (weight on the bed), `M572`
(pressure advance), `M900 K` (Linear Advance K values are used as the pressure
advance, both are filament mm per mm/s). Settings: `motion/motion.cpp`, EEPROM area
`EEPROM_INPUT_SHAPER`.

## Emulator tests

`pio run -e renode_test` builds a test variant (`-DEMULATION`) for the
[Renode](https://renode.io) emulator: the host serial is USART2, the EEPROM is preset
like a calibrated printer, the heaters, thermistors (with the response lag of the
E3D V6 that the thermal model expects) and the hotend fan tachometer are simulated,
and the LCD contents are mirrored into RAM. `test/renode/run.py`
boots it, sends G-code and checks the serial output and the LCD;
`test/renode/smoke_test.sh` checks start-up, M115, M503 and moves,
`test/renode/heat_test.sh` heating and temperature regulation,
`test/renode/lang_test.sh` the language menu and a German message,
`test/renode/motion_test.sh` fast moves at the MK3.5 limits with input shaping and
pressure advance (exact end positions, no step underruns) and
`test/renode/endstop_test.sh` an endstop stopping a move. The emulator does not model
the TMC2209 drivers, EXTI and the ADC, so these tests cover the firmware logic, not the
hardware layer.

`test/motion/run.sh` builds the motion engine for the host and compares its steps with
an exact (double precision) model of the shaped motion.

## Display

### Color TFT (default)

The Prusa UI writes to a 20x4 HD44780 character LCD (`lcd.cpp`). `lcd_tft.cpp`
emulates the HD44780 DDRAM and draws it on a 480x320 ST7796S display:

- **Menus and messages**: 4 rows of 20 cells of 24x80 pixels, anti-aliased font
  (JetBrains Mono, all characters of the 14 languages), the selected item (`>` in
  column 0) as an orange bar, the Prusa symbols as icons. `lcd_print_custom()` sends
  the Prusa character codes directly, so the 8 CGRAM slot limit of the HD44780 (and its
  fallback characters) does not apply.
- **Status screen**: `lcdui_print_status_screen()` marks its drawing with
  `tft_status_begin/end()`; its fields (fixed columns) are shown as tiles: temperatures
  in big digits, Z, speed, MMU slot, progress with a bar, time, and the status line.
  Anything else written to the screen switches back to the text layout.
- **Transfer**: no frame buffer (300 KB). A change marks the rectangle it affects in a
  list of 10 pixel high bands. A priority 3 software interrupt (TIM7 pended) renders the
  changed part of a band into one of two 9.6 KB buffers and sends it with SPI2 (16 MHz)
  and DMA, rendering the next band while the DMA runs. A temperature change sends one
  tile (~28k pixels, 3.5 ms), a new menu the whole screen (~150 ms).
- **Fonts**: `tools/tft_fonts.py` generates `tft_fonts.h` (2 bit anti-aliased, run
  length encoded, 27 KB).

`test/tft/run.sh` runs the driver on the host against a model of the ST7796S (commands,
address window, pixels): every sequence of changes, interleaved with the transfer, must
give the same picture as a complete redraw; `test/tft/run.sh DIR` writes screenshots.

### Creality Ender-3 display (`-DLCD_ENDER3`)

With the Creality Ender-3 display, `lcd_send()` hands the HD44780 commands to
`lcd_st7920.cpp`, which emulates the HD44780: DDRAM with its address layout and
wrap-around, the 8 CGRAM custom characters (the Prusa slot management in `lcd.cpp`
is unchanged) and the character ROM (font A00, `lcd_st7920_font.h`). Each character
is a 6x16 pixel cell (the 5x8 glyph doubled vertically) in a 128x64 frame buffer;
20 x 4 cells fill the screen with a 4 pixel margin left and right.

The ST7920 is driven in serial mode (3 wires, bit-banged at about 1.5 MHz, timing as
in Marlin for this board). A low priority interrupt (TIM7, every 40 µs, priority 3)
compares the frame buffer with what the display shows and sends one byte of the
changed 16 bit words per tick, so the UI never waits for the display: a changed
value is on the screen in about 2 ms, a full screen in about 50 ms, at about 30% of
the CPU during the transfer and 0% when nothing changes. Menus that clear the screen
and draw the same text again cause no traffic. `lcd_refresh()` (called by the UI to
revive a disturbed display) sets the controller up again and resends the screen.

`test/lcd/run.sh` runs the ST7920 driver on the host against a model of the ST7920 serial
interface and graphic RAM, and compares the picture with an independent rendering
of the expected text (custom characters, address wrap, interleaved updates, refresh
during a transfer). `test/lcd/run.sh screen.pgm` writes a picture of the test screen, and
`test/renode/run.py --screen screen.pgm ...` the picture the emulated firmware sent to
the display.

## FYSETC Cheetah V3.0 (STM32F446RC)

The same HAL supports the STM32F446 of the Cheetah V3.0 (`env:MK3S_CHEETAH_V3`,
`pins_FYSETC_CHEETAH_V3.h`, `variants/MK3S-CHEETAH_V3.h`, Arduino variant
`buildroot/variants/MARLIN_F446VE` from Marlin). Differences to the SKR Mini E3 V3:

| | SKR Mini E3 V3 (STM32G0B1) | Cheetah V3 (STM32F446RC) |
|---|---|---|
| CPU | Cortex-M0+ 64 MHz, no FPU | Cortex-M4F 180 MHz, FPU |
| step / soft PWM / temp. timers | TIM17 / TIM16 / TIM15 | TIM7 / TIM13 / TIM14 (APB1 timers at 90 MHz) |
| heatbed PWM | TIM1 CH2 (PC9) | TIM3 CH3 (PC8), 58.6 kHz instead of 62.5 kHz |
| ADC | G0 ADC (CHSELR), temp. sensor IN12, VREFINT IN13 | F4 ADC (SQR3), temp. sensor IN18, VREFINT IN17 |
| EEPROM | AT24C32 I2C | flash sectors 1+2: RAM copy, snapshot + byte log (`eeprom_flash.cpp`); compaction (a sector erase stalls the CPU up to 0.5 s) only when the printer is idle |
| TMC2209 UART | USART4 (PC10/PC11) | PB3 bit-banged at 230400 Bd with the DWT cycle counter; driver addresses detected at start-up from the IOIN DIR bit |
| flash | 512 KiB, 14 languages | 256 KiB: English only (`LANG_MODE=0`), LTO, compact single precision printf/strtod (`compact_stdio.cpp`) instead of newlib's (-12 KiB), image split around the EEPROM sectors (`buildroot/ld/cheetah_v3.ld`) |
| display | color TFT / Ender-3 / MK3S LCD | MK3S LCD on EXP1/EXP2 (HD44780 driver of the Prusa code, unchanged) |
| free pins | MMU, power panic, fan tachs | only USART1 (PA9/PA10): MMU. No power panic, no fan check |

Flash use: 15.1 of 16 KiB in sector 0, 199 of 208 KiB in sectors 3..5; RAM 52 of 128 KiB.

The emulator test build `env:renode_cheetah` runs on `test/renode/stm32f446.repl`
(Renode's STM32F4 platform with the timer clocks of the firmware and a model of the
DWT cycle counter): `run.py --board cheetah ...`.

## TMC2209 instead of TMC2130

The register map of the TMC2209 is close to the TMC2130 one. `tmc2130.cpp` keeps its
interface and translates:

- **Currents.** The SKR uses 0.11 Ω sense resistors (EINSy: 0.22 Ω). The Prusa current
  values (0-63, M911/M912/M913) keep their meaning; they are converted so that the
  motor current is the same as on the EINSy.
- **StallGuard.** StallGuard4 (SGTHRS/SG_RESULT) replaces StallGuard2 (COOLCONF SGT).
  StallGuard4 only works in stealthChop, so the axes that need stall detection run in
  stealthChop where the TMC2130 used spreadCycle: X/Y during homing and in the Normal
  mode when crash detection is on, Z always. With crash detection off, X/Y use
  spreadCycle in the Normal mode (full torque for the MK3.5 speeds); the setting is
  applied when crash detection is switched in the menu. The extruder keeps
  spreadCycle. Define `TMC2209_NORMAL_MODE_SPREADCYCLE` to use spreadCycle on X/Y in
  the Normal mode in any case (crash detection is then not available).
  The thresholds are `TMC2209_SGTHRS_*` in the variant file, `M916 X<n> Y<n>`
  changes them at run time. **Higher values are more sensitive** (the opposite of the
  TMC2130).
- **DIAG** is active high on the TMC2209. The DIAG pins are flagged with
  `FASTIO_INVERTED` so the Prusa code sees the TMC2130 (active low) levels.
- **Not available:** the programmable microstep table (linearity correction,
  `TMC2130_LINEARITY_CORRECTION`), XDIRECT, VDCMIN, THIGH.
