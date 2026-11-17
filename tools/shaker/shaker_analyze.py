#!/usr/bin/env python3
"""Find the resonance of an axis from an accelerometer recording and the printer's shake log.

usage: shaker_analyze.py ACCEL.csv SHAKE.log [--column 1|2|3] [--scale 1.0]

ACCEL.csv  time in seconds, then the x, y, z acceleration (Klipper raw data or an ESP32 stream:
           "#time,accel_x,accel_y,accel_z"; lines starting with # are skipped).
SHAKE.log  what shaker_run.py wrote: "<seconds> SHAKE ..." lines of the printer.
The two clocks do not have to agree: the start of the shaking is found in the recording and
matched to the first SHAKE F line. Prints the response per frequency and the M593 command.
"""
import math, sys, argparse

def read_csv(path):
    t, v = [], []
    for line in open(path):
        line = line.strip()
        if not line or line[0] == '#' or not (line[0].isdigit() or line[0] in '-.'):
            continue
        p = line.replace(';', ',').split(',')
        try:
            t.append(float(p[0])); v.append([float(p[1]), float(p[2]), float(p[3])])
        except (ValueError, IndexError):
            pass
    return t, v

def read_log(path):
    begin, steps, axis, end = None, [], None, None
    for line in open(path):
        p = line.split()
        if len(p) < 3 or p[1] != 'SHAKE':
            continue
        ts = float(p[0])
        if p[2] == 'BEGIN':
            axis = p[3]
            begin = {kv.split('=')[0]: int(kv.split('=')[1]) for kv in p[4:] if '=' in kv}
        elif p[2] == 'F':
            steps.append((ts, float(p[4]), float(p[5])))      # time, Hz, drive mm/s2
        elif p[2] == 'END':
            end = (ts, p[4] if len(p) > 4 else '')
    return begin, axis, steps, end

def dft_amplitude(t, x, f, t0, t1):
    """amplitude of the sinusoid at f in x between t0 and t1 (Hann window, mean removed)"""
    idx = [i for i in range(len(t)) if t0 <= t[i] < t1]
    if len(idx) < 8:
        return 0.0
    m = sum(x[i] for i in idx) / len(idx)
    re = im = ws = 0.0
    n = len(idx)
    for k, i in enumerate(idx):
        w = 0.5 - 0.5 * math.cos(2 * math.pi * (k + 0.5) / n)
        a = 2 * math.pi * f * (t[i] - t0)
        re += (x[i] - m) * w * math.cos(a)
        im += (x[i] - m) * w * math.sin(a)
        ws += w
    return 2 * math.hypot(re, im) / ws

def analyze(freq, resp):
    """returns (f, damping, peak, floor, second) or (None, reason)"""
    n = len(freq)
    if n < 7:
        return None, 'too few points'
    rs = [resp[0]] + [0.25 * resp[i - 1] + 0.5 * resp[i] + 0.25 * resp[i + 1] for i in range(1, n - 1)] + [resp[-1]]
    imax = max(range(n), key=lambda i: rs[i])
    floor = sorted(rs)[n // 4]
    peak = rs[imax]
    if imax in (0, n - 1):
        return None, 'peak at the edge of the range (%.0f Hz)' % freq[imax]
    if peak <= 0 or peak < 2.0 * floor:
        return None, 'no clear peak (peak %.3g, floor %.3g)' % (peak, floor)
    a, b, c = rs[imax - 1], rs[imax], rs[imax + 1]
    den = a - 2 * b + c
    delta = 0.5 * (a - c) / den if den < -1e-12 else 0.0
    f0 = freq[imax] + delta * (freq[imax + 1] - freq[imax])
    thr = peak * 0.70710678
    fl = fr = -1.0
    for j in range(imax, 0, -1):
        if rs[j - 1] < thr:
            fl = freq[j - 1] + (thr - rs[j - 1]) / (rs[j] - rs[j - 1]) * (freq[j] - freq[j - 1]); break
    for j in range(imax, n - 1):
        if rs[j + 1] < thr:
            fr = freq[j] + (rs[j] - thr) / (rs[j] - rs[j + 1]) * (freq[j + 1] - freq[j]); break
    z = 0.1
    if fl > 0 and fr > 0: z = min(f0 - fl, fr - f0) / f0   # the narrower side
    elif fl > 0: z = (f0 - fl) / f0
    elif fr > 0: z = (fr - f0) / f0
    z = min(0.3, max(0.04, z))
    second = 0.0
    for i in range(1, n - 1):
        if rs[i] >= rs[i - 1] and rs[i] > rs[i + 1] and rs[i] >= 0.6 * peak and abs(freq[i] - f0) > 5:
            second = freq[i]; break
    return (f0, z, peak, floor, second), 'ok'

def onset(t, v, col, fs):
    """time at which the shaking starts: first time the 0.1 s RMS passes 3x the quiet level"""
    n = max(4, int(0.1 * fs))
    x = [r[col] for r in v]
    def rms(a, b):
        m = sum(x[a:b]) / (b - a)
        return math.sqrt(sum((q - m) ** 2 for q in x[a:b]) / (b - a))
    base = max(rms(0, n), 1e-9)
    for i in range(n, len(x) - n, n // 2):
        if rms(i, i + n) > 3 * base + 0.02 * max(abs(q) for q in x[:n] or [1]):
            return t[i]
    return None

def run(t, v, log, col, scale):
    begin, axis, steps, end = log
    if not steps:
        raise SystemExit('no SHAKE F lines in the log')
    if len(t) < 100:
        raise SystemExit('recording too short')
    fs = (len(t) - 1) / (t[-1] - t[0])
    settle = begin.get('settle_ms', 150) / 1000.0
    window = begin.get('window_ms', 300) / 1000.0
    t_on = onset(t, v, col, fs)
    if t_on is None:
        raise SystemExit('no shaking found in the recording (sensor fixed? recording started?)')
    shift = t_on - steps[0][0]       # printer log time -> recording time
    x = [r[col] * scale for r in v]
    freq, resp = [], []
    for ts, f, drive in steps:
        a = dft_amplitude(t, x, f, ts + shift + settle, ts + shift + settle + window)
        freq.append(f); resp.append(a / (drive * 4 / math.pi))
    return fs, axis, freq, resp

def main():
    ap = argparse.ArgumentParser()
    ap.add_argument('csv'); ap.add_argument('log')
    ap.add_argument('--column', type=int, default=1, help='1=x 2=y 3=z of the recording (the axis the sensor measures along the shaken axis)')
    ap.add_argument('--scale', type=float, default=1.0, help='multiply samples to mm/s2 (9806.65 for g, 1000 for m/s2)')
    a = ap.parse_args()
    t, v = read_csv(a.csv)
    log = read_log(a.log)
    fs, axis, freq, resp = run(t, v, log, a.column - 1, a.scale)
    print('sample rate %.0f Hz, axis %s' % (fs, axis))
    for f, r in zip(freq, resp):
        print('%5.1f Hz  %8.4f  %s' % (f, r, '#' * int(min(60, 40 * r / (max(resp) or 1)))))
    res, why = analyze(freq, resp)
    if not res:
        print('\nNo result: ' + why); sys.exit(1)
    f0, z, peak, floor, second = res
    print('\nresonance %.1f Hz, damping %.2f (peak %.2f, floor %.2f)' % (f0, z, peak, floor))
    if second: print('second peak near %.0f Hz: take ZVD or a lower frequency if prints show ringing' % second)
    print('enter in Calibration > Input shaper, PA, or send:\nM593 %s T2 F%.1f D%.2f   (T2 = MZV)' % (axis, f0, z))

if __name__ == '__main__':
    main()
