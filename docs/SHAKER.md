# Measuring the input shaper resonance with an external accelerometer

The printer has **no accelerometer and no automatic shaper calibration**. It can only *shake* an
axis through a range of frequencies (15..95 Hz); an external Raspberry Pi (or ESP32) with an
accelerometer such as the BTT S2DW (LIS2DW12) records the motion; a script on the Pi finds the
resonance. You then enter the shaper type, frequency and damping by hand:
**Calibration > Input shaper, PA** (or `M593`).

> Status: the printer side and the analysis are tested only in the emulator and on synthetic
> data (`python3 tools/shaker/test_shaker_analyze.py`). Nothing here has run on a real printer
> or a real sensor yet, and the Klipper steps below are from the documentation, not tried.

## Printer side

* **Calibration > Shake X / Shake Y** (printer homed, Normal mode, not printing): after a prompt
  the axis shakes for about a minute with rising frequency. Click cancels.
* The same from a host: `M975 X` or `M975 Y`, optionally `S<start Hz> E<end Hz> I<step Hz>`.
* Serial output, one line per event (time-stamp them on the host):

```
SHAKE BEGIN X settle_ms=150 window_ms=300 dwell_ms=600
SHAKE F X 47.0 3012       <- frequency (Hz) and the drive acceleration (mm/s^2) of this step
SHAKE END X ok            (or: cancelled / failed ...)
```

The shaper of the axis is switched off while shaking and restored afterwards; acceleration and
jerk are restored too.

## Mounting

X: sensor on the nozzle carriage. Y: sensor on the heatbed. Fix it rigidly. Keep the X axis
(sensor along the shaken axis) in mind: use `--column 1/2/3` for the sensor axis that points
along the shaken axis.

## Raspberry Pi + ADXL345 on SPI (e.g. Mellow Fly ADXL345)

The simplest setup: the sensor on the Pi's SPI pins, the Pi on the printer's USB-C port, one
script (`tools/shaker/adxl345_pi.py`) that records and runs the sweep at the same time.

### Wiring (Raspberry Pi 3B, SPI0, 40-pin header)

| ADXL345 signal (silkscreen names vary) | Pi pin | Pi signal |
|---|---|---|
| GND                                   | 6 (or 9, 14, 20, 25) | GND |
| VCC / 3V3                             | 1 or 17 | 3.3 V |
| CS                                    | 24 | GPIO8, SPI0 CE0 |
| SDO (MISO)                            | 21 | GPIO9, SPI0 MISO |
| SDA / SDI (MOSI)                      | 19 | GPIO10, SPI0 MOSI |
| SCL / SCK / SCLK                      | 23 | GPIO11, SPI0 SCLK |
| INT1 / INT2                           | not connected | |

Check the silkscreen of your board before powering it: some boards have a 5 V input with a
regulator (then use 5 V, pin 2 or 4); a bare ADXL345 must get **3.3 V only**. The Pi's GPIOs are
3.3 V. Keep the wires short (under ~30 cm) and twisted with GND if possible: long loose SPI wires
on a moving carriage give read errors.

### Pi setup (once)

```
sudo raspi-config            # Interface Options > SPI > enable, reboot
sudo apt install python3-spidev python3-serial
ls /dev/spidev0.0            # must exist
python3 tools/shaker/adxl345_pi.py --test
```
`--test` reads the sensor for 2 s: it must report about 800 samples per second and about
9.8 m/s^2 on the axis that points up. "ADXL345 not found" means wiring or power.

Connect the Pi to the printer's USB-C port. Stop OctoPrint (or anything else using the port)
while measuring. The port is normally `/dev/ttyACM0`.

### Measuring

1. Home the printer (Normal mode, not printing).
2. Fix the sensor rigidly: on the nozzle carriage for X, on the bed for Y.
3. `python3 tools/shaker/adxl345_pi.py /dev/ttyACM0 X` (then the same with `Y` and the sensor on
   the bed). The printer shakes for about a minute; the script writes `shake_x_<date>.csv/.log`
   and prints the result, e.g. `M593 X T2 F47.1 D0.10`.
4. Enter frequency (and type) in Calibration > Input shaper, PA, or send the M593 line.

The script finds the sensor axis that moves by itself, so the sensor orientation does not matter.

## Other sensors (Klipper, ESP32)

1. Sensor: the S2DW is an RP2040 board that only runs Klipper firmware (vendor information), so
   the Pi runs Klipper with the S2DW as its only MCU (`[printer] kinematics: none`, a `[lis2dw]`
   section, see the Klipper "Measuring Resonances" documentation) and records with
   `ACCELEROMETER_MEASURE`. A different way to get a CSV of `time,x,y,z` (an ESP32 streaming a
   sensor, a USB capture, ...) works the same: the analysis only needs a CSV.
2. Start the recording, then run the sweep and write the printer log:

```
python3 tools/shaker/shaker_run.py /dev/ttyACM0 X shake_x.log
```
3. Stop the recording and analyse (the two clocks need not agree: the start of the shaking is
   found in the recording):

```
python3 tools/shaker/shaker_analyze.py accel.csv shake_x.log --column 1 [--scale 9806.65 if in g]
```

It prints the response per frequency, the resonance, the damping and a line like
`M593 X T2 F47.1 D0.10` (T2 = MZV). Enter the same values on the printer
(Calibration > Input shaper, PA). When there is a clear second peak, try ZVD or a lower
frequency. If there is no clear peak, or it is at the edge of the range, the script says so:
check the mounting.

The analysis needs a sample rate of several hundred Hz (the S2DW can do 1600 Hz).
