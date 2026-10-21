# Prusa Firmware for the BigTreeTech SKR Mini E3 V3.0

This is [Prusa-Firmware 3.14.1](https://github.com/prusa3d/Prusa-Firmware/releases/tag/v3.14.1)
for the **Original Prusa i3 MK3S**, ported from the 8-bit EINSy RAMBo (ATmega2560)
to the 32-bit **BigTreeTech SKR Mini E3 V3.0** (STM32G0B1RET6, TMC2209 drivers).

The printer logic is Prusa's own code, not a reimplementation: the same G-code
handling and planner, mesh bed leveling (G80) with the
P.I.N.D.A., XYZ calibration, PINDA temperature calibration, thermal model, LCD menus
and wizard, self test, crash detection, power panic recovery, filament sensor,
MMU2S/MMU3 support, Live Z / steel sheets, statistics and so on. What changed is the
hardware layer underneath it (`Firmware/hal_stm32`), which reproduces the timers,
ADC, EEPROM, serial lines and stepper drivers of the EINSy on the STM32. See
[docs/PORTING.md](docs/PORTING.md) for the details.

The stepper interrupt is the one part that is new: a motion engine
(`Firmware/motion`) with the **input shaper and pressure advance of the Original Prusa
MK3.5** (Prusa-Firmware-Buddy), which is the same mechanics as the MK3S on a 32-bit
board. It uses the MK3.5 default input shaper profile and allows the MK3.5 speeds and
accelerations, see [Input shaping and MK3.5 speeds](#input-shaping-and-mk35-speeds).

> **Status: compiles and runs in an emulator, not yet tested on a printer.** The
> firmware builds for the SKR Mini E3 V3 and passes the emulator tests (start-up,
> moves, input shaping, pressure advance, endstops, heating, languages), but it has not
> been run on real hardware yet. Expect to tune the StallGuard thresholds and to verify
> the wiring. Keep a hand on the power switch for the first homing and the first heat-up,
> and raise the speeds step by step.

## Build

With [PlatformIO](https://platformio.org/) (command line or the VS Code extension):

```
pio run
```

The result is `.pio/build/MK3S_SKR_MINI_E3_V3/firmware.bin`. Every push to GitHub
also builds it (Actions tab, artifact `firmware-MK3S_SKR_MINI_E3_V3`).

The translations (`lang/po`) are added to `firmware.bin` after linking by
`buildroot/scripts/lang.py`, which needs the Python packages `polib` and `pyelftools`
(installed automatically if missing). Flash `firmware.bin`, not `firmware.elf`: only
the `.bin` contains the language tables.

## Install

1. Copy `firmware.bin` to the root of a FAT32 micro-SD card.
2. Put the card into the SKR Mini E3 V3 and power the board. The BigTreeTech
   bootloader flashes the file and renames it to `FIRMWARE.CUR`.
3. Connect over USB at 115200 Bd (any baud rate works, it is a USB CDC port).

On the first start the settings EEPROM of the board is erased if it contains the
settings of another firmware, the printer then starts like a new MK3S: run the
**Wizard** (self test, XYZ calibration, first layer calibration).

## Wiring

See [docs/WIRING.md](docs/WIRING.md) and the [wiring picture](docs/wiring.png). In short: motors, heaters and thermistors go to
the usual headers, the P.I.N.D.A. to the PROBE header (its thermistor to SERVO with a
4.7 kΩ pull-up), the IR filament sensor to E0-STOP, the TMC2209 DIAG jumpers X/Y/Z
must be installed, and the MK3S LCD needs a custom harness (EXP1 plus three spare
pins).

## Feature status

| Prusa MK3S feature | On the SKR Mini E3 V3 |
|---|---|
| G-code, planner, arcs, MBL (G80), XYZ calibration (G45/M45), Z calibration, PINDA temp. calibration (G76), Live Z, steel sheets | Prusa code unchanged |
| Input shaper (MK3.5) | **new**: MZV X 50.7 Hz / Y 40.6 Hz (MK3.5 defaults), Y lowered with the bed weight (M74); M593 to change |
| Pressure advance (MK3.5, M572) | **new**, replaces Linear Advance 1.5; M900 K values are used as the pressure advance |
| Speeds / accelerations | MK3.5 limits: up to 300 mm/s and 7000 mm/s² on X/Y (M203/M201), MK3.5 defaults and jerk |
| LCD menus, wizard, self test, statistics, fail stats | unchanged, original 20x4 LCD |
| Print from SD | on-board micro-SD slot |
| Print over USB (PrusaSlicer, OctoPrint, M862 checks) | native USB, reports as MK3S |
| Thermal model, temperature runaway, min/max temp protection | unchanged; "ambient" is the STM32 temperature sensor instead of the EINSy board thermistor |
| Sensorless homing, crash detection, belt test | TMC2209 StallGuard4, thresholds need tuning (M916). StallGuard4 needs stealthChop: with crash detection **on**, X/Y run in stealthChop (less torque at high speed); with it **off**, X/Y use spreadCycle for the MK3.5 speeds. See [docs/PORTING.md](docs/PORTING.md) |
| Normal / Stealth mode, motor currents (M911-M913), microstepping (M350) | yes, currents converted for the 0.11 Ω sense resistors |
| IR filament sensor, runout, autoload | yes (digital input) |
| Fan check, Noctua/alt-fan detection | yes, needs the tach wires (see wiring) |
| Power panic recovery | yes, needs a power-fail signal on PWR-DET |
| MMU2S / MMU3 | yes, on the TFT header (no reset wire, reset over serial) |
| Quiet heatbed PWM | same automaton on a hardware timer |
| Languages | all 14 languages of the Prusa release (en, cs, de, es, fr, it, pl, nl, ro, hu, hr, sk, sv, no), stored in the internal flash and selectable directly from the menu |
| Crash dumps (D20-D22), memory dump menu | **no** (needs the external SPI flash) |
| TMC2130 linearity correction | **no** (the TMC2209 has no programmable wave table) |
| LCD backlight dimming, PSU/bed voltage readout, Raspberry Pi port, MMU reset wire | **no** (not available on the board) |
| Firmware update over USB (PrusaSlicer) | **no**, use the SD card bootloader |

## Input shaping and MK3.5 speeds

The motion engine applies the input shaper of the MK3.5 to X and Y, and pressure
advance to the extruder. Its defaults are those of the MK3.5:

| Setting | Default | G-code |
|---|---|---|
| X shaper | MZV, 50.7 Hz, damping 0.1 | `M593 X F50.7 T2 D0.1` |
| Y shaper | MZV, 40.6 Hz, damping 0.1 | `M593 Y F40.6 T2 D0.1` |
| Y weight adjust | -20 Hz at 800 g on the bed | `M593 A-20 M800`, `M74 W<grams>` |
| Pressure advance | off | `M572 S<seconds> W<smooth time>`, or `M900 K<value>` |
| Max. feedrate X/Y | 200 mm/s (limit 300) | `M203` |
| Max. acceleration X/Y | 1250 mm/s² (limit 7000) | `M201`, `M204` |

`M593` without parameters prints the settings and the motion engine statistics,
`M593 W` stores the input shaper settings in the EEPROM, `M593 X F0` disables the
shaper of an axis.

To print faster, keep using the **MK3S** PrusaSlicer printer profile (its start G-code
is the one this firmware understands: G80, M862.3 P "MK3S"...) and raise the machine
limits and the speeds of the print profile towards the MK3.5 values. MK3.5 G-code
files cannot be printed as they are: they use Buddy specific commands (G29, M555...).

For speed, switch **crash detection off** (Settings menu): the TMC2209 can only detect
stalls in stealthChop, which loses torque at high speed; with crash detection off X/Y
run in spreadCycle like on the MK3.5.

The input shaper frequencies of the MK3.5 were measured by Prusa on the MK3.5 / MK3S
mechanics. Belts, frame and bed weight make them differ slightly between printers;
they can be changed with M593 (there is no accelerometer calibration).

## Repository layout

| Path | Content |
|---|---|
| `Firmware/` | Prusa-Firmware sources (the first commit is the unmodified v3.14.1) |
| `Firmware/hal_stm32/` | STM32 hardware abstraction layer |
| `Firmware/motion/` | motion engine: input shaper, pressure advance, step generation |
| `Firmware/pins_SKR_MINI_E3_V3.h` | pin map |
| `Firmware/variants/MK3S-SKR_MINI_E3_V3.h` | printer configuration (MK3S values, hardware related changes marked `SKR:`) |
| `buildroot/` | PlatformIO board definition, STM32 variant (from Marlin), build scripts |
| `lang/` | Prusa translations (`po/`) and language table tools |
| `test/renode/` | emulator test harness (see [docs/PORTING.md](docs/PORTING.md)) |
| `test/motion/` | host unit test of the motion engine against an exact model |
| `docs/` | wiring and porting notes |
| `README_PRUSA.md` | the original Prusa-Firmware README |

## License

GPL v3, like Prusa-Firmware (see `LICENSE`). The STM32 board definition and variant
files are taken from [Marlin](https://github.com/MarlinFirmware/Marlin) (GPL v3) and
STMicroelectronics (BSD 3-clause). "Original Prusa" and "Prusa" are trademarks of
Prusa Research; this project is not affiliated with Prusa Research or BigTreeTech.
