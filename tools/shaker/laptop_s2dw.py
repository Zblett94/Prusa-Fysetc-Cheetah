#!/usr/bin/env python3
"""Laptop (macOS, Linux, Windows): BTT S2DW with the USB firmware (s2dw_usb) and the printer,
both on USB of the laptop. Records the sensor while the printer shakes an axis, then analyses.

usage: laptop_s2dw.py X|Y [--sensor PORT] [--printer PORT]
       laptop_s2dw.py --test            (sensor only: 2 s of data, prints averages)

The ports are found by their USB ids (sensor: Raspberry Pi RP2040, printer: STMicroelectronics)
unless given. Needs Python 3 and pyserial (pip3 install pyserial). The printer must be homed,
idle and in Normal mode; nothing else may use its USB port. See docs/SHAKER.md.
"""
import argparse, os, sys, threading, time

HERE = os.path.dirname(os.path.abspath(__file__))
sys.path.insert(0, HERE)
from adxl345_pi import run_printer  # noqa: E402

SCALE = 9.80665 * 1.952 / 4        # mm/s^2 per count, as Klipper (LIS2DW12, +-16 g)


def find_ports():
    from serial.tools import list_ports
    sensor = printer = None
    for p in list_ports.comports():
        if p.vid == 0x2E8A and sensor is None:
            sensor = p.device
        elif p.vid == 0x0483 and printer is None:
            printer = p.device
    return sensor, printer


class Sensor:
    def __init__(self, port):
        import serial
        self.s = serial.Serial(port, 115200, timeout=0.2)
        time.sleep(0.3)
        self.s.reset_input_buffer()
        self.samples, self.marks, self.overflows = [], [], 0
        self.running = False

    def command(self, c, expect, timeout=3.0):
        self.s.write(c.encode())
        t = time.monotonic() + timeout
        while time.monotonic() < t:
            line = self.s.readline().decode(errors='replace').strip()
            if line.startswith(expect):
                return line
            if line.startswith('ERROR'):
                raise SystemExit('sensor: ' + line)
        raise SystemExit('sensor does not answer (%r): is the S2DW flashed with s2dw_usb.uf2?' % c)

    def _reader(self):
        while self.running:
            line = self.s.readline().decode(errors='replace').strip()
            if line:
                if line[0] == 'T':
                    _, us, n = line.split()
                    self.marks.append((int(us), int(n)))
                elif line[0] == 'O':
                    self.overflows += 1
                elif line[0] in '-0123456789':
                    p = line.split()
                    if len(p) == 3:
                        self.samples.append((int(p[0]), int(p[1]), int(p[2])))
                elif line.startswith('STOPPED'):
                    self.running = False

    def start(self):
        self.command('s', 'STARTED')
        self.running = True
        self.th = threading.Thread(target=self._reader, daemon=True)
        self.th.start()

    def stop(self):
        self.s.write(b'x')
        self.th.join(5)
        self.running = False

    def rate(self):
        if len(self.marks) < 2:
            return 1600.0
        (u0, n0), (u1, n1) = self.marks[0], self.marks[-1]
        return (n1 - n0) / ((u1 - u0) * 1e-6)


def main():
    ap = argparse.ArgumentParser()
    ap.add_argument('axis', nargs='?', choices=['X', 'Y', 'x', 'y'])
    ap.add_argument('--sensor'); ap.add_argument('--printer')
    ap.add_argument('--test', action='store_true')
    a = ap.parse_args()
    sensor_port, printer_port = find_ports()
    sensor_port = a.sensor or sensor_port
    printer_port = a.printer or printer_port
    if not sensor_port:
        raise SystemExit('S2DW not found (USB id 2e8a): plugged in and flashed with s2dw_usb.uf2?')
    sen = Sensor(sensor_port)
    print(sen.command('i', 'S2DW'))

    if a.test:
        sen.start(); time.sleep(2); sen.stop()
        n = len(sen.samples)
        avg = [sum(s[i] for s in sen.samples) / max(n, 1) * SCALE / 1000 for i in range(3)]
        print('%d samples, %.0f Hz, %d overflows' % (n, sen.rate(), sen.overflows))
        print('mean x %.2f y %.2f z %.2f m/s^2 (one axis about 9.8: gravity)' % tuple(avg))
        return
    if not a.axis:
        ap.error('axis X or Y needed (or --test)')
    if not printer_port:
        raise SystemExit('printer not found (USB id 0483): give it with --printer')
    axis = a.axis.upper()
    name = 'shake_%s_%s' % (axis.lower(), time.strftime('%Y%m%d_%H%M%S'))
    t_ref = time.monotonic()
    sen.start()
    time.sleep(0.5)
    try:
        ok = run_printer(printer_port, axis, name + '.log', t_ref)
    finally:
        time.sleep(0.5)
        sen.stop()
    rate = sen.rate()
    with open(name + '.csv', 'w') as f:
        f.write('#time,accel_x,accel_y,accel_z\n')
        for i, s in enumerate(sen.samples):
            f.write('%.6f,%.1f,%.1f,%.1f\n' % (i / rate, s[0] * SCALE, s[1] * SCALE, s[2] * SCALE))
    print('%d samples (%.0f Hz), %d FIFO overflows -> %s.csv, %s.log' % (len(sen.samples), rate, sen.overflows, name, name))
    if sen.overflows:
        print('warning: samples were lost (laptop busy or USB hub): close other programs and repeat')
    if not ok:
        raise SystemExit('the sweep did not finish (see the log)')
    os.execv(sys.executable, [sys.executable, os.path.join(HERE, 'shaker_analyze.py'), name + '.csv', name + '.log', '--column', '0'])


if __name__ == '__main__':
    main()
