#!/usr/bin/env python3
"""Run the shake sweep (M975) on the printer and write a timestamped log of its SHAKE lines.

usage: shaker_run.py /dev/ttyACM0 X|Y SHAKE.log [BAUD=115200]
Start the accelerometer recording first (Klipper: ACCELEROMETER_MEASURE, or the ESP32), run
this, stop the recording when it says done. Needs pyserial (pip install pyserial).
The printer must be homed and idle, in Normal mode.
"""
import sys, time
import serial

port, axis, out = sys.argv[1], sys.argv[2].upper(), sys.argv[3]
baud = int(sys.argv[4]) if len(sys.argv) > 4 else 115200
s = serial.Serial(port, baud, timeout=0.5)
time.sleep(1)
s.reset_input_buffer()
s.write(('M975 %s\n' % axis).encode())
t0 = time.time()
with open(out, 'w') as f:
    while time.time() - t0 < 300:
        line = s.readline().decode(errors='replace').strip()
        if 'SHAKE' not in line:
            continue
        f.write('%.3f %s\n' % (time.time() - t0, line)); f.flush()
        print(line)
        if 'SHAKE END' in line:
            break
print('done')
