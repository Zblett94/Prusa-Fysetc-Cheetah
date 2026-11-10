# Generates docs/wiring_cheetah.svg: python3 tools/wiring_diagram_cheetah.py > docs/wiring_cheetah.svg
# Connector positions follow FYSETC's board render (github.com/FYSETC/Cheetah_V3.0), simplified.
import html
W, H = 1960, 1400
BX0, BY0, BX1, BY1 = 600, 330, 1330, 900   # board outline
out = []
def esc(s): return html.escape(s, quote=True)
def text(x, y, s, size=14, anchor='start', weight='normal', fill='#1f2933'):
    out.append(f'<text x="{x}" y="{y}" font-size="{size}" text-anchor="{anchor}" font-weight="{weight}" fill="{fill}">{esc(s)}</text>')
def rect(x, y, w, h, fill, stroke='none', rx=4, sw=1.5):
    out.append(f'<rect x="{x}" y="{y}" width="{w}" height="{h}" rx="{rx}" fill="{fill}" stroke="{stroke}" stroke-width="{sw}"/>')
def line(pts, color):
    d = 'M' + ' L'.join(f'{x},{y}' for x, y in pts)
    out.append(f'<path d="{d}" stroke="{color}" stroke-width="2" fill="none"/>')
    out.append(f'<circle cx="{pts[0][0]}" cy="{pts[0][1]}" r="3.5" fill="{color}"/>')

C = {'motor': '#2563eb', 'power': '#dc2626', 'sensor': '#059669', 'fan': '#7c3aed',
     'lcd': '#d97706', 'comm': '#475569', 'unused': '#9ca3af', 'jumper': '#b45309'}

def box(bx, by, bw, title, lines, group):
    col = C[group]
    bh = 24 + 17 * len(lines)
    rect(bx, by, bw, bh, '#ffffff', col, 6, 2)
    rect(bx, by, 6, bh, col, 'none', 3)
    text(bx + 14, by + 18, title, 14, weight='bold', fill=col)
    for i, l in enumerate(lines):
        text(bx + 14, by + 36 + 17 * i, l, 12.5)
    return bh

def callout(p, bx, by, bw, title, lines, group, via=None, side=None):
    """connector point p -> label box; the leader ends on the given side of the box"""
    bh = 24 + 17 * len(lines)
    if side is None:
        side = 'right' if p[0] > bx + bw else 'left' if p[0] < bx else ('bottom' if p[1] > by else 'top')
    end = {'right': (bx + bw, by + bh / 2), 'left': (bx, by + bh / 2),
           'top': (min(max(p[0], bx + 20), bx + bw - 20), by),
           'bottom': (min(max(p[0], bx + 20), bx + bw - 20), by + bh)}[side]
    line([p] + (via or []) + [end], C[group])
    box(bx, by, bw, title, lines, group)

def conn(x, y, w, h, name, group, size=11.5):
    rect(x, y, w, h, '#f8fafc', C[group], 3, 2)
    text(x + w / 2, y + h / 2 + 4, name, size, 'middle', 'bold', '#0f172a')
    return (x, y, w, h)

def T(c): return (c[0] + c[2] / 2, c[1])
def B(c): return (c[0] + c[2] / 2, c[1] + c[3])
def R(c): return (c[0] + c[2], c[1] + c[3] / 2)
def L(c): return (c[0], c[1] + c[3] / 2)

out.append(f'<svg xmlns="http://www.w3.org/2000/svg" width="{W}" height="{H}" viewBox="0 0 {W} {H}" font-family="Helvetica, Arial, sans-serif">')
rect(0, 0, W, H, '#f1f5f9', rx=0)
text(W / 2, 46, 'Original Prusa i3 MK3S  →  FYSETC Cheetah V3.0', 28, 'middle', 'bold', '#0f172a')
text(W / 2, 74, 'Where every cable plugs in (firmware: pio run -e MK3S_CHEETAH_V3). Simplified layout: positions are approximate, go by the names printed on the board.', 15, 'middle', fill='#475569')

# board
rect(BX0, BY0, BX1 - BX0, BY1 - BY0, '#111827', '#000000', 14, 3)
rect(BX0 + 10, BY0 + 10, BX1 - BX0 - 20, BY1 - BY0 - 20, 'none', '#374151', 10, 1)
text(BX0 + 30, BY0 + 300, 'Cheetah V3.0', 24, 'start', 'bold', '#e5e7eb')
text(BX0 + 30, BY0 + 322, 'STM32F446RC · 4× TMC2209', 13, fill='#9ca3af')
rect(BX0 + 500, BY0 + 265, 80, 80, '#1f2937', '#4b5563', 4)
text(BX0 + 540, BY0 + 310, 'STM32', 12, 'middle', 'bold', '#9ca3af')

# top edge: motors, SD
mot = {}
for name, x in (('X', 20), ('Y', 150), ('Z', 262), ('Z ', 374), ('E', 486)):
    mot[name] = conn(BX0 + x, BY0 + 14, 100, 34, name.strip() + ' motor', 'motor')
    rect(BX0 + x + 27, BY0 + 110, 46, 46, '#1f2937', '#4b5563', 3)
    text(BX0 + x + 50, BY0 + 138, 'TMC', 10.5, 'middle', 'bold', '#9ca3af')
sd = conn(BX0 + 618, BY0 + 14, 96, 70, 'microSD', 'lcd')
usb = conn(BX0 + 650, BY0 + 150, 64, 50, 'USB-C', 'comm')
exp1 = conn(BX0 + 596, BY0 + 262, 56, 150, 'EXP1', 'lcd')
exp2 = conn(BX0 + 660, BY0 + 262, 56, 150, 'EXP2', 'lcd')
exp3 = conn(BX0 + 596, BY0 + 422, 120, 40, 'EXP3', 'unused')

# DIAG jumpers, BT0, Pi header
diag = []
for n, x in (('X', 110), ('Y', 180), ('Z', 430)):
    rect(BX0 + x, BY0 + 200, 58, 24, '#fbbf24', '#92400e', 3)
    text(BX0 + x + 29, BY0 + 216, n + '-DIAG', 10.5, 'middle', 'bold', '#78350f')
    diag.append((BX0 + x, BY0 + 212))
bt0 = conn(BX0 + 500, BY0 + 200, 86, 24, '3V3 B0 G', 'jumper', 10.5)
pi = conn(BX0 + 300, BY0 + 236, 120, 30, 'Pi header', 'comm')

# middle 3-pin connectors and the fan voltage jumpers
mid = conn(BX0 + 300, BY0 + 345, 46, 50, 'Probe', 'sensor', 10.5)
fil = conn(BX0 + 352, BY0 + 345, 46, 50, 'FIL', 'sensor', 10.5)
rgb = conn(BX0 + 404, BY0 + 345, 46, 50, 'RGB', 'sensor', 10.5)
for i in range(3):
    rect(BX0 + 296 + i * 52, BY0 + 425, 46, 20, '#fbbf24', '#92400e', 3)
    text(BX0 + 319 + i * 52, BY0 + 439, '5 12 24', 9.5, 'middle', 'bold', '#78350f')
text(BX0 + 296, BY0 + 462, 'fan voltage jumpers', 10.5, fill='#fde68a')

# bottom edge
pw = conn(BX0 + 14, BY0 + 516, 74, 40, '24V in', 'power')
bed = conn(BX0 + 96, BY0 + 516, 74, 40, 'Bed', 'power')
he = conn(BX0 + 178, BY0 + 516, 74, 40, 'E0 heat', 'power')
bot = {}
for i, (n, g) in enumerate((('FAN0', 'fan'), ('FAN1', 'fan'), ('FAN2', 'unused'), ('X-MIN', 'unused'), ('Y-MIN', 'unused'),
                            ('Z-MIN', 'unused'), ('T0', 'sensor'), ('T1', 'sensor'), ('T2', 'sensor'))):
    bot[n] = conn(BX0 + 268 + i * 50, BY0 + 516, 46, 40, n, g, 10)

# ---- callouts -------------------------------------------------------------
LX, LW = 40, 440       # left column
RX, RW = 1480, 440     # right column
callout(T(mot['X']), LX, 120, LW, 'X motor', ['X-axis stepper motor (XYE cable bundle).', 'MCU: step PC0, dir PC1, enable PC3.'], 'motor',
        via=[(T(mot['X'])[0], 270), (520, 270), (520, 150)], side='right')
callout(T(mot['Y']), LX, 200, LW, 'Y motor', ['Y-axis stepper motor.', 'MCU: step PC14, dir PC13, enable PC15.'], 'motor',
        via=[(T(mot['Y'])[0], 290), (540, 290), (540, 230)], side='right')
zl, zr = T(mot['Z']), T(mot['Z '])
callout(zl, 700, 110, 330, 'Z motors (both)', ['Left and right Z motor on the two Z', 'connectors (one driver, as on the EINSy).'], 'motor',
        via=[(zl[0], 250)], side='bottom')
line([zr, (zr[0], 250), (zl[0], 250)], C['motor'])
callout(T(mot['E']), 1060, 110, 370, 'Extruder motor', ['FYSETC NEMA 14 on the BMG 3:1 extruder.', 'Firmware limits it to 0.33 A. If it turns', 'backwards: turn the plug around.'], 'motor', side='bottom')

callout(diag[0], LX, 290, LW, 'Jumpers to set (amber on the board)', [
    'X, Y, Z DIAG: installed (sensorless homing, crash detection).',
    'Fan voltage of FAN0 and FAN1 (5 / 12 / 24): on 5.',
    'Probe voltage jumper: on 5V.',
    'B0 (boot): on G for normal use; on 3V3 only to flash.'], 'jumper',
        via=[(560, diag[0][1]), (560, 336)], side='right')

callout(B(pw), LX, 430, LW, 'Power supply 24 V', ['PSU + and − to the power input terminals.', 'Check the polarity printed on the board.'], 'power',
        via=[(B(pw)[0], 960), (540, 960), (540, 460)], side='right')
callout(B(bed), LX, 510, LW, 'Heatbed MK52', ['Both bed wires to the Heated-Bed terminals', '(on-board MOSFET, MCU PC8).'], 'power',
        via=[(B(bed)[0], 980), (520, 980), (520, 540)], side='right')
callout(B(he), LX, 590, LW, 'Hotend heater (40 W)', ['To the E0 heater terminals (MCU PC7).'], 'power',
        via=[(B(he)[0], 1000), (500, 1000), (500, 612)], side='right')

callout(B(bot['FAN0']), LX, 1040, LW, 'Print fan (5 V blower)', ['FAN0, voltage jumper on 5. Red = +, black = −.', 'Leave the tachometer wire unconnected.'], 'fan',
        via=[(B(bot['FAN0'])[0], 1070)], side='right')
callout(B(bot['FAN1']), LX, 1120, LW, 'Hotend fan (5 V Noctua)', ['FAN1, voltage jumper on 5.', 'Leave the tachometer wire unconnected.'], 'fan',
        via=[(B(bot['FAN1'])[0], 1150)], side='right')
callout(B(bot['X-MIN']), LX, 1200, LW, 'Leave empty', ['FAN2, X-MIN, Y-MIN, Z-MIN, EXP3 and the PSU', 'power panic cable are not used. Nothing may be', 'plugged into X/Y/Z-MIN with the DIAG jumpers on.'], 'unused',
        via=[(B(bot['X-MIN'])[0], 1240)], side='right')

t0, t1, t2 = B(bot['T0']), B(bot['T1']), B(bot['T2'])
callout(t1, 1080, 1010, 360, 'Thermistors', ['T0: hotend (MCU PC5)', 'T1: heatbed (MCU PB0)', 'T2: P.I.N.D.A. v2 (MCU PB1); a SuperPINDA', 'has none (see docs/WIRING_CHEETAH.md)'], 'sensor', side='top')
line([t0, (t0[0], 960), (t1[0], 960)], C['sensor']); line([t2, (t2[0], 960), (t1[0], 960)], C['sensor'])

callout(R(sd), RX, 120, RW, 'SD card', ['Use the slot of the LCD or this one,', 'not both at once (same SPI bus).'], 'lcd', via=[(1440, R(sd)[1]), (1440, 150)], side='left')
callout(R(usb), RX, 210, RW, 'USB-C', ['PC / OctoPrint, and flashing (DFU) with the', 'B0 jumper on 3V3.'], 'comm', via=[(1420, R(usb)[1]), (1420, 240)], side='left')
callout(R(exp2), RX, 300, RW, 'MK3S LCD: EXP1 and EXP2', ['The two flat cables of the original LCD:', 'P1 → EXP1, P2 → EXP2. Check the orientation', '(EXP1 pin 1 = 5 V, pin 2 = GND): a wrong', 'way round shows a blank or garbled screen.'], 'lcd',
        via=[(1400, R(exp2)[1]), (1400, 346)], side='left')
e1 = T(exp1)
line([e1, (e1[0], BY0 + 246), (BX1 + 40, BY0 + 246), (BX1 + 40, R(exp2)[1])], C['lcd'])

# the three small connectors in the middle leave the board through the free strip above the bottom row
y1, y2, y3 = BY0 + 478, BY0 + 490, BY0 + 502
pr, fl, rg = B(mid), B(fil), B(rgb)
callout(pr, RX, 440, RW, 'P.I.N.D.A. probe', ['Probe connector: signal, 5 V, GND (MCU PA0).', 'Probe voltage jumper on 5V.'], 'sensor',
        via=[(pr[0], BY0 + 410), (BX0 + 280, BY0 + 410), (BX0 + 280, y3), (1380, y3), (1380, 470)], side='left')
callout(fl, RX, 520, RW, 'IR filament sensor', ['FIL-DET: signal and GND (MCU PA1).', '5 V for the sensor from the RGB (Neopixel)', 'connector. Schottky diode (BAT85) in the', 'signal wire, band toward the sensor.'], 'sensor',
        via=[(fl[0], BY0 + 404), (BX0 + 470, BY0 + 404), (BX0 + 470, y2), (1360, y2), (1360, 570)], side='left')
line([rg, (rg[0], BY0 + 404)], C['sensor'])
callout(R(pi), RX, 630, RW, 'MMU2S / MMU3 (optional)', ['Raspberry Pi header UART, no Pi needed:', 'TX1 (PA9) → MMU RX', 'RX1 (PA10) ← MMU TX through a divider:', '1 kΩ in series, 2 kΩ to GND (MMU is 5 V)', 'GND ↔ GND. MMU power from the PSU.'], 'comm',
        via=[(BX0 + 484, R(pi)[1]), (BX0 + 484, y1), (1340, y1), (1340, 690)], side='left')

# legend
lx, ly = 1480, 1040
rect(lx, ly, 440, 250, '#ffffff', '#cbd5e1', 8)
text(lx + 16, ly + 26, 'Colours', 15, weight='bold')
for i, (g, n) in enumerate((('motor', 'Stepper motors'), ('power', 'Power, heaters'), ('sensor', 'Sensors, thermistors'),
                            ('fan', 'Fans'), ('lcd', 'LCD, SD card'), ('comm', 'USB, MMU'), ('jumper', 'Jumpers'), ('unused', 'Not used'))):
    rect(lx + 18 + (i % 2) * 210, ly + 44 + (i // 2) * 28, 18, 14, C[g], rx=3)
    text(lx + 44 + (i % 2) * 210, ly + 56 + (i // 2) * 28, n, 13)
text(lx + 16, ly + 180, 'First start: run the Wizard, then', 13)
text(lx + 16, ly + 198, 'Calibration > E-steps cal.', 13)
text(lx + 16, ly + 222, 'Not tested on hardware yet: check each', 13, fill='#b91c1c')
text(lx + 16, ly + 240, 'connection against the board silkscreen.', 13, fill='#b91c1c')
out.append('</svg>')
print('\n'.join(out))
