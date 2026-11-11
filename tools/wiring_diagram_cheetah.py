# Generates docs/wiring_cheetah.svg: python3 tools/wiring_diagram_cheetah.py > docs/wiring_cheetah.svg
# Dark theme, numbered connectors. The board layout follows FYSETC's render of the
# Cheetah V3.0 (github.com/FYSETC/Cheetah_V3.0), simplified.
import html
W, H = 2000, 1085
BG, PANEL, EDGE, TXT, MUTED = '#0d1117', '#161b22', '#30363d', '#e6edf3', '#8b949e'
C = {'motor': '#58a6ff', 'power': '#ff7b72', 'fan': '#bc8cff', 'sensor': '#3fb950',
     'lcd': '#f0883e', 'comm': '#39c5cf', 'jumper': '#e3b341', 'unused': '#6e7681'}
FONT = "Inter, 'Segoe UI', Helvetica, Arial, sans-serif"
out = []
def esc(s): return html.escape(s, quote=True)
def text(x, y, s, size=14, anchor='start', weight='400', fill=TXT, spacing=None):
    ls = f' letter-spacing="{spacing}"' if spacing else ''
    out.append(f'<text x="{x:.1f}" y="{y:.1f}" font-size="{size}" text-anchor="{anchor}" font-weight="{weight}" fill="{fill}"{ls}>{esc(s)}</text>')
def rect(x, y, w, h, fill, stroke='none', rx=4, sw=1.5, extra=''):
    out.append(f'<rect x="{x:.1f}" y="{y:.1f}" width="{w:.1f}" height="{h:.1f}" rx="{rx}" fill="{fill}" stroke="{stroke}" stroke-width="{sw}" {extra}/>')
def circle(x, y, r, fill, stroke='none', sw=1.5):
    out.append(f'<circle cx="{x:.1f}" cy="{y:.1f}" r="{r}" fill="{fill}" stroke="{stroke}" stroke-width="{sw}"/>')
def badge(x, y, label, group, r=13):
    circle(x, y, r, C[group], BG, 2)
    text(x, y + 4.5, label, 12.5 if len(label) < 2 else 11.5, 'middle', '700', '#0d1117')

# ---- board (coordinates of FYSETC's render, 855 x 590, scaled) -----------------
BX, BY, S = 585, 190, 0.97
def bx(v): return BX + v * S
def by(v): return BY + v * S
def part(x0, y0, x1, y1, name, group, size=11, dim=False, pins=0, vertical=False):
    """a connector on the board; returns its rectangle in page coordinates"""
    x, y, w, h = bx(x0), by(y0), (x1 - x0) * S, (y1 - y0) * S
    col = C[group]
    rect(x, y, w, h, '#10151c' if dim else '#1b222c', col, 4, 1.2 if dim else 2, 'opacity="0.75"' if dim else '')
    if pins:  # pin dots along the long side
        for i in range(pins):
            if vertical: circle(x + w / 2, y + h * (i + 0.5) / pins, 1.6, col)
            else: circle(x + w * (i + 0.5) / pins, y + h - 7, 1.6, col)
    if name:
        text(x + w / 2, y + (h / 2 + 4 if not pins or vertical else h / 2 + 1), name, size, 'middle', '600', MUTED if dim else TXT)
    return (x, y, w, h)

out.append(f'<svg xmlns="http://www.w3.org/2000/svg" width="{W}" height="{H}" viewBox="0 0 {W} {H}" font-family="{FONT}">')
out.append('<defs><linearGradient id="pcb" x1="0" y1="0" x2="1" y2="1"><stop offset="0" stop-color="#151b24"/><stop offset="1" stop-color="#0a0e14"/></linearGradient>'
           '<filter id="sh" x="-10%" y="-10%" width="120%" height="130%"><feDropShadow dx="0" dy="6" stdDeviation="10" flood-color="#000" flood-opacity="0.55"/></filter></defs>')
rect(0, 0, W, H, BG, rx=0)

# header
text(60, 64, 'MK3S  →  FYSETC Cheetah V3.0', 30, weight='700')
text(60, 92, 'WIRING GUIDE', 13, weight='600', fill=MUTED, spacing='2.5')
text(W - 60, 58, 'Prusa-Firmware 3.14.1  ·  pio run -e MK3S_CHEETAH_V3', 14, 'end', fill=MUTED)
text(W - 60, 80, 'Simplified layout: go by the names printed on the board', 14, 'end', fill=MUTED)
out.append(f'<line x1="60" y1="112" x2="{W - 60}" y2="112" stroke="{EDGE}" stroke-width="1"/>')

# board body
rect(bx(22), by(0), 818 * S, 572 * S, 'url(#pcb)', '#3d444d', 16, 2, 'filter="url(#sh)"')
rect(bx(30), by(8), 802 * S, 556 * S, 'none', '#222a35', 11, 1)
for hx, hy in ((190, 38), (668, 38), (298, 325), (793, 527)):
    circle(bx(hx), by(hy), 13, '#0d1117', '#c9a227', 4)
text(bx(52), by(292), 'Cheetah V3.0', 21, weight='700', fill='#c9d1d9')
text(bx(52), by(314), 'STM32F446 · 4× TMC2209', 11.5, fill=MUTED)
# chips
for cx, n in ((112, 'X'), (268, 'Y'), (422, 'Z'), (578, 'E')):
    rect(bx(cx - 22), by(140), 44 * S, 44 * S, '#0a0d12', '#2b323c', 3, 1)
    text(bx(cx - 1), by(160), 'TMC', 8.5, 'middle', '600', '#59636e'); text(bx(cx - 1), by(172), n, 9.5, 'middle', '700', '#7d8590')
rect(bx(556), by(292), 80 * S, 80 * S, '#0a0d12', '#2b323c', 4, 1)
text(bx(595), by(336), 'STM32', 10.5, 'middle', '600', '#59636e')
circle(bx(74), by(398), 34, '#0a0d12', '#2b323c', 1.5)   # capacitor
rect(bx(40), by(446), 62 * S, 30 * S, '#12305c', '#1f4b8f', 4, 1)  # fuse

mx = part(55, 12, 155, 56, 'X', 'motor', 12, pins=4)
my = part(215, 12, 315, 56, 'Y', 'motor', 12, pins=4)
mz1 = part(325, 12, 425, 56, 'Z', 'motor', 12, pins=4)
mz2 = part(432, 12, 532, 56, 'Z', 'motor', 12, pins=4)
me = part(540, 12, 640, 56, 'E', 'motor', 12, pins=4)
sd = part(708, 24, 828, 132, 'microSD', 'lcd', 11.5)
usb = part(762, 178, 828, 248, 'USB-C', 'comm', 11)
exp1 = part(672, 268, 744, 416, 'EXP1', 'lcd', 11.5)
exp2 = part(756, 268, 828, 416, 'EXP2', 'lcd', 11.5)
exp3 = part(668, 432, 828, 498, 'EXP3', 'unused', 11, dim=True)
pi = part(352, 262, 472, 300, 'Pi header', 'comm', 10.5)
boot = part(538, 232, 640, 256, '3V3 B0 G', 'jumper', 9.5)
jx = part(118, 214, 178, 236, 'X-DIAG', 'jumper', 9)
jy = part(206, 232, 266, 254, 'Y-DIAG', 'jumper', 9)
jz = part(462, 212, 522, 234, 'Z-DIAG', 'jumper', 9)
probe = part(342, 364, 386, 430, 'Probe', 'sensor', 9.5)
fil = part(392, 364, 436, 430, 'FIL', 'sensor', 9.5)
rgb = part(442, 364, 486, 430, 'RGB', 'sensor', 9.5)
fj0 = part(340, 440, 386, 458, '5 12 24', 'jumper', 8)
fj1 = part(390, 440, 436, 458, '5 12 24', 'jumper', 8)
fj2 = part(440, 440, 486, 458, '5 12 24', 'unused', 8, dim=True)
bl = part(530, 462, 640, 490, 'BLTouch', 'unused', 9.5, dim=True)
pw = part(52, 504, 128, 560, '24V IN', 'power', 10.5)
bed = part(142, 504, 218, 560, 'BED', 'power', 10.5)
he = part(232, 504, 308, 560, 'E0', 'power', 10.5)
bot = {}
for i, (n, g) in enumerate((('FAN0', 'fan'), ('FAN1', 'fan'), ('FAN2', 'unused'), ('X-MIN', 'unused'), ('Y-MIN', 'unused'),
                            ('Z-MIN', 'unused'), ('T0', 'sensor'), ('T1', 'sensor'), ('T2', 'sensor'))):
    bot[n] = part(338 + i * 48.5, 504, 380 + i * 48.5, 560, n, g, 9, dim=(g == 'unused'))

def top(c): return (c[0] + c[2] / 2, c[1] - 20)
def below(c): return (c[0] + c[2] / 2, c[1] + c[3] + 20)
def right(c): return (c[0] + c[2] + 22, c[1] + c[3] / 2)
def corner(c): return (c[0] + c[2] - 2, c[1] + 2)
def stem(c, p, group):
    out.append(f'<line x1="{c[0] + c[2] / 2:.1f}" y1="{(c[1] if p[1] < c[1] else c[1] + c[3]):.1f}" x2="{p[0]:.1f}" y2="{p[1]:.1f}" stroke="{C[group]}" stroke-width="1.5" opacity="0.6"/>')
def hstem(c, p, group):
    out.append(f'<line x1="{c[0] + c[2]:.1f}" y1="{p[1]:.1f}" x2="{p[0]:.1f}" y2="{p[1]:.1f}" stroke="{C[group]}" stroke-width="1.5" opacity="0.6"/>')

for c, n in ((mx, '1'), (my, '2'), (mz1, '3'), (mz2, '3'), (me, '4')):
    stem(c, top(c), 'motor'); badge(*top(c), n, 'motor')
for c, n, g in ((pw, '5', 'power'), (bed, '6', 'power'), (he, '7', 'power'), (bot['FAN0'], '8', 'fan'), (bot['FAN1'], '9', 'fan'),
                (bot['T0'], '10', 'sensor'), (bot['T1'], '11', 'sensor'), (bot['T2'], '12', 'sensor')):
    stem(c, below(c), g); badge(*below(c), n, g)
for c, n, g in ((sd, '16', 'lcd'), (usb, '17', 'comm'), (exp2, '15', 'lcd')):
    hstem(c, right(c), g); badge(*right(c), n, g)
badge(*corner(exp1), '15', 'lcd', 11)
badge(*corner(probe), '13', 'sensor', 11); badge(*corner(fil), '14', 'sensor', 11); badge(*corner(rgb), '14', 'sensor', 11)
badge(*corner(pi), '18', 'comm', 11)
for c in (jx, jy, jz): badge(*corner(c), 'A', 'jumper', 10)
for c in (fj0, fj1): badge(c[0] + c[2] / 2, c[1] + c[3] + 13, 'B', 'jumper', 10)
badge(*corner(boot), 'D', 'jumper', 10)

# ---- side panels ---------------------------------------------------------------
def section(x, y, w, title, group, items, note=None):
    col = C[group]
    h = 44 + sum(30 + 17 * len(l) for _, _, l in items) + (8 + 16 * len(note) if note else 0)
    rect(x, y, w, h, PANEL, EDGE, 10, 1)
    rect(x, y + 12, 4, 20, col, rx=2)
    text(x + 18, y + 27, title.upper(), 12.5, weight='700', fill=col, spacing='1.6')
    cy = y + 46
    for b, t, lines in items:
        if b: badge(x + 30, cy + 9, b, group, 12)
        text(x + 54, cy + 14, t, 14.5, weight='600')
        for i, l in enumerate(lines):
            text(x + 54, cy + 33 + 17 * i, l, 12.5, fill=MUTED)
        cy += 30 + 17 * len(lines)
    if note:
        for i, l in enumerate(note):
            text(x + 18, cy + 12 + 16 * i, l, 12, fill='#d29922')
    return y + h + 16

LX, RX, PW = 60, 1470, 470
y = 136
y = section(LX, y, PW, 'Motors', 'motor', [
    ('1', 'X motor', ['X-axis stepper']),
    ('2', 'Y motor', ['Y-axis stepper']),
    ('3', 'Z motors', ['Left and right motor on the two Z connectors']),
    ('4', 'Extruder motor', ['FYSETC NEMA 14 on the BMG 3:1 extruder (0.33 A).', 'Runs backwards? Turn the plug around.'])])
y = section(LX, y, PW, 'Power and heaters', 'power', [
    ('5', 'Power supply 24 V', ['PSU + and − ; check the polarity marks']),
    ('6', 'Heatbed MK52', ['Both bed wires']),
    ('7', 'Hotend heater', ['40 W heater cartridge'])],
    note=['Terminal order not confirmed: read the board labels.'])
y = section(LX, y, PW, 'Fans', 'fan', [
    ('8', 'Print fan', ['5 V blower on FAN0. Tachometer wire unconnected.']),
    ('9', 'Hotend fan', ['5 V Noctua on FAN1. Tachometer wire unconnected.'])])
y = section(LX, y, PW, 'Jumpers', 'jumper', [
    ('A', 'X, Y, Z DIAG: installed', ['Sensorless homing and crash detection']),
    ('B', 'Fan voltage FAN0, FAN1: 5', ['The MK3S fans are 5 V fans']),
    ('C', 'Probe voltage: 5V', ['Next to the Probe connector (not drawn)']),
    ('D', 'B0: on G', ['On 3V3 only while flashing over USB'])])

y = 136
y = section(RX, y, PW, 'Display and connections', 'lcd', [
    ('15', 'MK3S LCD', ['Flat cables P1 → EXP1, P2 → EXP2.', 'Blank or garbled screen: a plug is the wrong way round.']),
    ('16', 'SD card', ['Use this slot or the one of the LCD, not both'])])
y = section(RX, y, PW, 'USB and MMU', 'comm', [
    ('17', 'USB-C', ['PC / OctoPrint, and flashing']),
    ('18', 'MMU2S / MMU3 (optional)', ['TX1 → MMU RX.  GND ↔ GND.', 'RX1 ← MMU TX through 1 kΩ, with 2 kΩ to GND.', 'No Raspberry Pi needed.'])])
y = section(RX, y, PW, 'Sensors', 'sensor', [
    ('10', 'Hotend thermistor', ['T0']),
    ('11', 'Heatbed thermistor', ['T1']),
    ('12', 'P.I.N.D.A. thermistor', ['T2 (PINDA v2 only; a SuperPINDA has none)']),
    ('13', 'P.I.N.D.A. probe', ['Signal, 5 V and GND on the Probe connector']),
    ('14', 'IR filament sensor', ['Signal and GND on FIL-DET, 5 V from RGB.', 'BAT85 diode in the signal wire, band to the sensor.'])],
    note=['Order of Probe / FIL / RGB not confirmed: read the board labels.'])

# ---- below the board -----------------------------------------------------------
fy = by(572) + 70
fx, fw = bx(22), 818 * S
rect(fx, fy, fw, 118, PANEL, EDGE, 10, 1)
rect(fx, fy + 12, 4, 20, C['unused'], rx=2)
text(fx + 18, fy + 27, 'LEAVE EMPTY', 12.5, weight='700', fill=C['unused'], spacing='1.6')
text(fx + 18, fy + 54, 'FAN2 · X-MIN · Y-MIN · Z-MIN · EXP3 · BLTouch · PSU power panic cable', 14, weight='600')
text(fx + 18, fy + 76, 'Nothing may be plugged into X/Y/Z-MIN while the DIAG jumpers are installed.', 12.5, fill=MUTED)
text(fx + 18, fy + 98, 'Power panic and the fan speed check are not used in this firmware.', 12.5, fill=MUTED)

ly = fy + 138
rect(fx, ly, fw, 92, PANEL, EDGE, 10, 1)
for i, (g, n) in enumerate((('motor', 'Motors'), ('power', 'Power, heaters'), ('fan', 'Fans'), ('sensor', 'Sensors'),
                            ('lcd', 'Display, SD'), ('comm', 'USB, MMU'), ('jumper', 'Jumpers'), ('unused', 'Not used'))):
    cx = fx + 22 + (i % 4) * (fw - 30) / 4
    cy = ly + 24 + (i // 4) * 26
    circle(cx + 6, cy, 6, C[g]); text(cx + 22, cy + 4.5, n, 13, fill='#c9d1d9')
text(fx + 22, ly + 78, 'First start: run the Wizard, then Calibration › E-steps cal.   Not yet tested on hardware.', 12.5, fill='#d29922')
out.append('</svg>')
print('\n'.join(out))
