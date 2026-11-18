#!/usr/bin/env python3
"""Raspberry Pi + BTT S2DW (LIS2DW12 behind an RP2040 running Klipper): measure one axis.

usage: s2dw_klipper.py PRINTER_PORT X|Y [--klippy ~/printer_data/comms/klippy.serial]
                       [--chip lis2dw]

Klipper runs on the Pi with the S2DW as its only MCU (config: docs/SHAKER.md). This script
starts ACCELEROMETER_MEASURE through Klipper's command pipe, makes the printer (USB, the
Cheetah) shake the axis with M975, stops the measurement, and runs shaker_analyze.py on the
CSV Klipper writes to /tmp. The two clocks need not agree (the analysis aligns them).
Needs: python3-serial. The printer must be homed, idle and in Normal mode.
"""
import argparse, glob, os, sys, time

HERE = os.path.dirname(os.path.abspath(__file__))
sys.path.insert(0, HERE)
from adxl345_pi import run_printer  # noqa: E402  (does not need spidev)


def klippy(pipe, line):
    with open(pipe, 'w') as f:
        f.write(line + '\n')


def main():
    ap = argparse.ArgumentParser()
    ap.add_argument('port', help='printer serial port, e.g. /dev/serial/by-id/usb-STMicro...')
    ap.add_argument('axis', choices=['X', 'Y', 'x', 'y'])
    ap.add_argument('--klippy', default=os.path.expanduser('~/printer_data/comms/klippy.serial'),
                    help="Klipper's command pipe (klippy -I)")
    ap.add_argument('--chip', default='lis2dw', help='name of the accelerometer section in printer.cfg')
    a = ap.parse_args()
    axis = a.axis.upper()
    if not os.path.exists(a.klippy):
        raise SystemExit('%s not found: is Klipper running? (sudo systemctl status klipper)' % a.klippy)
    name = 'shake_%s_%s' % (axis.lower(), time.strftime('%Y%m%d_%H%M%S'))

    t_ref = time.monotonic()
    klippy(a.klippy, 'ACCELEROMETER_MEASURE CHIP=%s NAME=%s' % (a.chip, name))
    time.sleep(1.0)
    try:
        ok = run_printer(a.port, axis, name + '.log', t_ref)
    finally:
        time.sleep(0.5)
        klippy(a.klippy, 'ACCELEROMETER_MEASURE CHIP=%s NAME=%s' % (a.chip, name))
    # Klipper writes the file in the background
    csv = None
    for _ in range(60):
        found = sorted(glob.glob('/tmp/*%s*.csv' % name), key=os.path.getmtime)
        if found:
            size = os.path.getsize(found[-1])
            time.sleep(1.0)
            if os.path.getsize(found[-1]) == size and size > 0:
                csv = found[-1]
                break
        time.sleep(0.5)
    if not csv:
        raise SystemExit('no /tmp/*%s*.csv from Klipper: check the Klipper log (~/printer_data/logs/klippy.log)' % name)
    print('recording: %s, printer log: %s.log' % (csv, name))
    if not ok:
        raise SystemExit('the sweep did not finish (see the log)')
    os.execv(sys.executable, [sys.executable, os.path.join(HERE, 'shaker_analyze.py'), csv, name + '.log', '--column', '0'])


if __name__ == '__main__':
    main()
