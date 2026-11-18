#!/usr/bin/env python3
"""Raspberry Pi: record an ADXL345 on SPI while the printer shakes an axis, then analyse.

usage: adxl345_pi.py /dev/ttyACM0 X|Y [--rate 800|1600] [--spi 0.0] [--out NAME]

Wiring and setup: docs/SHAKER.md. Needs:  sudo apt install python3-spidev python3-serial
The printer (USB) must be homed, idle and in Normal mode. Writes NAME.csv (time, x, y, z in
mm/s^2) and NAME.log (the printer's SHAKE lines), then runs shaker_analyze.py on them.
Test the sensor alone with:  adxl345_pi.py --test
"""
import argparse, os, sys, threading, time

REG_DEVID, REG_BW_RATE, REG_POWER_CTL, REG_INT_SOURCE = 0x00, 0x2C, 0x2D, 0x30
REG_DATA_FORMAT, REG_DATAX0, REG_FIFO_CTL, REG_FIFO_STATUS = 0x31, 0x32, 0x38, 0x39
LSB_MM_S2 = 0.0039 * 9806.65          # full resolution: 3.9 mg per LSB


class ADXL345:
    def __init__(self, bus=0, dev=0, rate=800):
        import spidev
        self.spi = spidev.SpiDev()
        self.spi.open(bus, dev)
        self.spi.mode = 3
        self.spi.max_speed_hz = 5000000
        devid = self.rd(REG_DEVID)
        if devid != 0xE5:
            raise SystemExit('ADXL345 not found on SPI %d.%d (device id 0x%02x, expected 0xe5): check the wiring' % (bus, dev, devid))
        self.wr(REG_POWER_CTL, 0x00)                    # standby while configuring
        self.wr(REG_DATA_FORMAT, 0x0B)                  # 4-wire SPI, full resolution, +-16 g
        self.wr(REG_BW_RATE, {400: 0x0C, 800: 0x0D, 1600: 0x0E, 3200: 0x0F}[rate])
        self.wr(REG_FIFO_CTL, 0x00)                     # clear the FIFO
        self.wr(REG_FIFO_CTL, 0x80)                     # stream mode
        self.rate = rate

    def rd(self, reg):
        return self.spi.xfer2([0x80 | reg, 0])[1]

    def wr(self, reg, val):
        self.spi.xfer2([reg, val])

    def start(self):
        self.wr(REG_POWER_CTL, 0x08)

    def stop(self):
        self.wr(REG_POWER_CTL, 0x00)

    def read_fifo(self, out):
        """append the waiting samples (x, y, z raw) to out; returns True on FIFO overflow"""
        n = self.rd(REG_FIFO_STATUS) & 0x3F
        for _ in range(n):
            b = self.spi.xfer2([0xC0 | REG_DATAX0] + [0] * 6)
            out.append(tuple(int.from_bytes(bytes(b[i:i + 2]), 'little', signed=True) for i in (1, 3, 5)))
        return n >= 32 and (self.rd(REG_INT_SOURCE) & 0x01) != 0


def record(sensor, stop_event, samples, info):
    sensor.start()
    t0 = time.monotonic()
    overruns = 0
    while not stop_event.is_set():
        if sensor.read_fifo(samples):
            overruns += 1
        time.sleep(0.004)                               # FIFO holds 32 samples: 40 ms at 800 Hz
    sensor.read_fifo(samples)
    t1 = time.monotonic()
    sensor.stop()
    info.update(t0=t0, t1=t1, overruns=overruns)


def run_printer(port, axis, log_path, t_ref):
    import serial
    s = serial.Serial(port, 115200, timeout=0.5)
    time.sleep(1.5)
    s.reset_input_buffer()
    s.write(('M975 %s\n' % axis).encode())
    t_start = time.monotonic()
    ok = False
    with open(log_path, 'w') as f:
        while time.monotonic() - t_start < 300:
            line = s.readline().decode(errors='replace').strip()
            if 'SHAKE' not in line:
                continue
            f.write('%.4f %s\n' % (time.monotonic() - t_ref, line)); f.flush()
            if ' F ' in line:
                print('\r' + line + '      ', end='', flush=True)
            else:
                print('\n' + line)
            if 'SHAKE END' in line:
                ok = ' ok' in line
                break
    s.close()
    return ok


def main():
    ap = argparse.ArgumentParser()
    ap.add_argument('port', nargs='?'); ap.add_argument('axis', nargs='?')
    ap.add_argument('--rate', type=int, default=800, choices=[400, 800, 1600, 3200])
    ap.add_argument('--spi', default='0.0', help='bus.device, 0.0 = SPI0 CE0 (pin 24)')
    ap.add_argument('--out', default=None)
    ap.add_argument('--test', action='store_true', help='read the sensor for 2 s and print averages')
    a = ap.parse_args()
    bus, dev = (int(x) for x in a.spi.split('.'))
    sensor = ADXL345(bus, dev, a.rate)

    if a.test:
        samples, ev, info = [], threading.Event(), {}
        th = threading.Thread(target=record, args=(sensor, ev, samples, info)); th.start()
        time.sleep(2); ev.set(); th.join()
        n = len(samples)
        rate = n / (info['t1'] - info['t0'])
        avg = [sum(s[i] for s in samples) / max(n, 1) * LSB_MM_S2 / 1000 for i in range(3)]
        print('%d samples, %.0f Hz, %d overflows' % (n, rate, info['overruns']))
        print('mean x %.2f y %.2f z %.2f m/s^2 (one axis should show about 9.8: gravity)' % tuple(avg))
        return

    if not a.port or not a.axis or a.axis.upper() not in ('X', 'Y'):
        ap.error('port and axis X or Y needed (or --test)')
    axis = a.axis.upper()
    name = a.out or 'shake_%s_%s' % (axis.lower(), time.strftime('%Y%m%d_%H%M%S'))
    samples, ev, info = [], threading.Event(), {}
    t_ref = time.monotonic()
    th = threading.Thread(target=record, args=(sensor, ev, samples, info)); th.start()
    time.sleep(0.5)
    try:
        ok = run_printer(a.port, axis, name + '.log', t_ref)
    finally:
        time.sleep(0.5); ev.set(); th.join()
    n = len(samples)
    dt = (info['t1'] - info['t0']) / max(n, 1)          # the sensor clock, measured over the run
    with open(name + '.csv', 'w') as f:
        f.write('#time,accel_x,accel_y,accel_z\n')
        for i, s in enumerate(samples):
            f.write('%.5f,%.1f,%.1f,%.1f\n' % (info['t0'] - t_ref + i * dt, s[0] * LSB_MM_S2, s[1] * LSB_MM_S2, s[2] * LSB_MM_S2))
    print('%d samples (%.0f Hz), %d FIFO overflows -> %s.csv, %s.log' % (n, 1 / dt, info['overruns'], name, name))
    if info['overruns']:
        print('warning: samples were lost (Pi too busy?): try --rate 400')
    if not ok:
        raise SystemExit('the sweep did not finish (see the log)')
    here = os.path.dirname(os.path.abspath(__file__))
    os.execv(sys.executable, [sys.executable, os.path.join(here, 'shaker_analyze.py'), name + '.csv', name + '.log', '--column', '0'])


if __name__ == '__main__':
    main()
