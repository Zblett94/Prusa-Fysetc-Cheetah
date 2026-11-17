#!/usr/bin/env python3
"""Synthetic recording: a 47 Hz, damping 0.1 resonator driven by the sweep; the recording clock
starts at an arbitrary time. The analysis must find 47 Hz and a damping near 0.1."""
import math, os, random, sys, tempfile
sys.path.insert(0, os.path.dirname(__file__))
import shaker_analyze as sa

random.seed(1)
F0, Z, FS, DWELL = 47.0, 0.1, 1600.0, 0.6
def gain(f):
    r = f / F0; a = 1 - r * r; b = 2 * Z * r
    return math.sqrt(1 + b * b) / math.sqrt(a * a + b * b)

freqs = [15 + i for i in range(81)]
log_steps, segs, tl = [], [], 0.0
for f in freqs:
    drive = 3000.0
    log_steps.append((tl + 5.0, f, drive))       # log clock: starts 5 s after the connection
    segs.append((tl, f, drive * 4 / math.pi * gain(f)))
    tl += DWELL + 0.05                            # the planner drains between frequencies
rec_start = 1234.5                                # the recording clock is unrelated
t, v, i = [], [], 0
n_total = int((tl + 4) * FS)
lead = 1.7                                        # recording starts before the shaking
for k in range(n_total):
    ts = k / FS - lead                            # time relative to the first move
    amp, f = 0.0, 50.0
    for (s0, ff, a) in segs:
        if s0 <= ts < s0 + DWELL:
            amp, f = a, ff; break
    x = amp * math.sin(2 * math.pi * f * ts) + random.gauss(0, 30) + 50
    t.append(rec_start + k / FS); v.append([x, 0.1 * x, 9810.0])

begin = {'settle_ms': 150, 'window_ms': 300, 'dwell_ms': 600}
fs, axis, freq, resp = sa.run(t, v, (begin, 'X', log_steps, None), 0, 1.0)
res, why = sa.analyze(freq, resp)
ok = res is not None and abs(res[0] - F0) < 1.0 and 0.06 < res[1] < 0.16
print('result', res, why)
# a flat recording must be refused
flat = sa.analyze(freqs, [1.0 + 0.01 * random.random() for _ in freqs])[0] is None
# peak at the edge must be refused
edge = sa.analyze(freqs, [1.0 / (1 + (f - 15)) for f in freqs])[0] is None
print('flat refused', flat, 'edge refused', edge)
sys.exit(0 if (ok and flat and edge) else 1)
