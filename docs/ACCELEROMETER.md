# Input shaper calibration with an ADXL345

Calibration > **Shaper cal. X** / **Shaper cal. Y** shakes one axis at 15..95 Hz while an ADXL345
accelerometer records the motion, finds the resonance frequency and damping and sets the MZV
input shaper for that axis (stored in EEPROM, same as M593).

> Status: the analysis is tested on the host (`sh test/resonance/run.sh`) and the whole menu flow
> runs in the emulator against a synthetic sensor. The SPI driver, the step timing against a real
> sensor and the sweep itself are **untested on hardware**. Treat the first run as a trial.

## Wiring (3.3 V ADXL345 breakout with SPI)

The sensor uses the SD card lines of the LCD's EXP2 connector, so **remove the SD card from the
LCD** (the sensor shares the SD card's SPI lines, so the card slot cannot be used while it is fitted).

| ADXL345 | EXP2 pin | Cheetah |
|---------|----------|---------|
| SDO     | 10 (MISO)| PA6     |
| SDA/SDI | 5 (MOSI) | PA7     |
| SCL/SCK | 9 (SCK)  | PA5     |
| CS      | 7 (SDSS) | PA4     |
| VCC     | 1        | 5 V only if the breakout has a regulator, else use a 3.3 V pin |
| GND     | 2        | GND     |

Check that your breakout accepts the supply voltage you use: the ADXL345 itself is a 3.3 V part.

## Use

1. Fix the sensor rigidly: on the nozzle carriage for X, on the heated bed for Y (X axis
   parked in the middle).
2. Home all axes, then Calibration > Shaper cal. X (or Y), follow the prompts. The sweep takes
   about a minute and the axis shakes with growing frequency; hold the printer still.
3. The result screen shows resonance frequency and damping. Choose Yes to apply the MZV shaper.
4. The serial port prints `RES <axis> <Hz> <amplitude>` lines for plotting and a summary line.

If the sensor is not found, the signal is too weak (no clear peak) or the peak is at the edge of
the swept range, the calibration reports why and changes nothing.
