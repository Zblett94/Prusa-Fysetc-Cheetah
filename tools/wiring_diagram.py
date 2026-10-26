# Generates docs/wiring.svg: python3 tools/wiring_diagram.py > docs/wiring.svg
import html, sys
W, H = 1900, 1330
BX0, BY0, BX1, BY1 = 540, 300, 1280, 880   # board outline
out = []
def esc(s): return html.escape(s, quote=True)
def text(x, y, s, size=14, anchor='start', weight='normal', fill='#1f2933', cls=None):
    out.append(f'<text x="{x}" y="{y}" font-size="{size}" text-anchor="{anchor}" font-weight="{weight}" fill="{fill}">{esc(s)}</text>')
def rect(x, y, w, h, fill, stroke='none', rx=4, sw=1.5):
    out.append(f'<rect x="{x}" y="{y}" width="{w}" height="{h}" rx="{rx}" fill="{fill}" stroke="{stroke}" stroke-width="{sw}"/>')
def line(x1, y1, x2, y2, color):
    out.append(f'<path d="M{x1},{y1} L{x2},{y2}" stroke="{color}" stroke-width="2" fill="none"/>')
    out.append(f'<circle cx="{x1}" cy="{y1}" r="3.5" fill="{color}"/>')

C = {  # cable colour per group
    'motor': '#2563eb', 'power': '#dc2626', 'sensor': '#059669', 'fan': '#7c3aed',
    'lcd': '#d97706', 'comm': '#475569', 'unused': '#9ca3af'}

def callout(px, py, bx, by, bw, title, lines, group, attach=None):
    """connector point (px,py) -> label box at (bx,by)"""
    col = C[group]
    bh = 24 + 17 * len(lines)
    # attach the leader line to the nearest side of the box
    if attach == 'bottom': ax, ay = min(max(px, bx + 20), bx + bw - 20), by + bh
    elif px < bx: ax, ay = bx, by + bh / 2
    elif px > bx + bw: ax, ay = bx + bw, by + bh / 2
    else: ax, ay = px if bx + 20 < px < bx + bw - 20 else bx + bw / 2, (by if py < by else by + bh)
    line(px, py, ax, ay, col)
    rect(bx, by, bw, bh, '#ffffff', col, 6, 2)
    rect(bx, by, 6, bh, col, 'none', 3)
    text(bx + 14, by + 18, title, 14, weight='bold', fill=col)
    for i, l in enumerate(lines):
        text(bx + 14, by + 36 + 17 * i, l, 12.5)

def connector(x, y, w, h, name, group, vertical=False):
    rect(x, y, w, h, '#f8fafc', C[group], 3, 2)
    text(x + w / 2, y + h / 2 + 4.5, name, 12, 'middle', 'bold', '#0f172a')

out.append(f'<svg xmlns="http://www.w3.org/2000/svg" width="{W}" height="{H}" viewBox="0 0 {W} {H}" font-family="Helvetica, Arial, sans-serif">')
rect(0, 0, W, H, '#f1f5f9', rx=0)
text(W / 2, 46, 'Original Prusa i3 MK3S  →  BigTreeTech SKR Mini E3 V3.0', 28, 'middle', 'bold', '#0f172a')
text(W / 2, 74, 'Where every cable plugs in (firmware: Prusa-SKR-Mini). Simplified layout: positions are approximate, match the names printed on the board.', 15, 'middle', fill='#475569')

# board
rect(BX0, BY0, BX1 - BX0, BY1 - BY0, '#14532d', '#0b3d20', 14, 3)
rect(BX0 + 12, BY0 + 12, BX1 - BX0 - 24, BY1 - BY0 - 24, 'none', '#1e7a45', 10, 1)
text((BX0 + BX1) / 2, (BY0 + BY1) / 2 - 10, 'SKR Mini E3 V3.0', 26, 'middle', 'bold', '#d1fae5')
text((BX0 + BX1) / 2, (BY0 + BY1) / 2 + 18, 'STM32G0B1RET6 · 4× TMC2209 on board', 15, 'middle', fill='#a7f3d0')
# MCU chip
rect((BX0 + BX1) / 2 - 45, (BY0 + BY1) / 2 + 40, 90, 90, '#111827', '#374151', 4)
text((BX0 + BX1) / 2, (BY0 + BY1) / 2 + 90, 'STM32', 13, 'middle', 'bold', '#9ca3af')
# driver chips + DIAG jumpers
for i, n in enumerate(['X', 'Y', 'Z', 'E']):
    x = BX0 + 110 + i * 150
    rect(x, BY0 + 80, 46, 46, '#111827', '#374151', 3)
    text(x + 23, BY0 + 108, n, 13, 'middle', 'bold', '#9ca3af')
    on = n != 'E'
    rect(x + 54, BY0 + 88, 30, 30, '#fbbf24' if on else '#f8fafc', '#92400e', 3)
    text(x + 69, BY0 + 107, 'ON' if on else 'off', 10.5, 'middle', 'bold', '#78350f')
text(BX0 + 110, BY0 + 150, 'DIAG jumpers next to the drivers: X, Y, Z installed (sensorless homing,', 12.5, fill='#fde68a')
text(BX0 + 110, BY0 + 168, 'crash detection, Z calibration), E0 removed (E0-STOP is the filament sensor)', 12.5, fill='#fde68a')

# ---------------- top edge: motors ----------------
motors = [('XM', 'X motor', ['TMC2209, UART address 0']),
          ('YM', 'Y motor', ['TMC2209, UART address 2']),
          ('ZAM', 'Z motor (left)', ['both Z motors are driven', 'by one TMC2209, like on', 'the EINSy']),
          ('ZBM', 'Z motor (right)', ['second Z connector,', 'same driver as ZAM']),
          ('EM', 'Extruder motor', ['TMC2209, UART address 3'])]
for i, (name, title, lines) in enumerate(motors):
    x = BX0 + 60 + i * 120
    connector(x, BY0 - 4, 78, 30, name, 'motor')
    callout(x + 39, BY0 - 4, 120 + i * 250, 110, 230, title, lines, 'motor', 'bottom')

# ---------------- bottom edge: power, heaters, fans ----------------
bottom = [('POWER', 'power', 'PSU 24 V', ['+ / − (DCIN)', 'mind the polarity']),
          ('HB', 'power', 'Heatbed MK52', ['24 V, ~9 A: external', 'MOSFET module', 'recommended']),
          ('HE', 'power', 'Hotend heater', ['40 W heater cartridge']),
          ('FAN0', 'fan', 'Print fan', ['5 V blower', 'fan jumper on 5 V']),
          ('FAN1', 'fan', 'Hotend fan', ['5 V Noctua', 'fan jumper on 5 V']),
          ('FAN2', 'unused', 'FAN2', ['not used'])]
for i, (name, group, title, lines) in enumerate(bottom):
    x = BX0 + 60 + i * 115
    connector(x, BY1 - 26, 84, 30, name, group)
    callout(x + 42, BY1 + 4, 480 + i * 158, 990, 150, title, lines, group)

# ---------------- right edge ----------------
right = [('X-STOP', 'unused', 'X-STOP', ['not used: X homes on the', 'DIAG jumper (StallGuard)']),
         ('Y-STOP', 'unused', 'Y-STOP', ['not used (DIAG jumper)']),
         ('Z-STOP', 'unused', 'Z-STOP', ['not used (DIAG jumper)']),
         ('E0-STOP', 'sensor', 'IR filament sensor', ['signal + 5 V + GND (PC15)']),
         ('PROBE', 'sensor', 'P.I.N.D.A. / SuperPINDA', ['signal + 5 V + GND (PC14)', 'check the 5 V output level']),
         ('SERVO', 'sensor', 'P.I.N.D.A. thermistor', ['signal pin + GND (PA1)', 'ADD 4.7 kΩ PA1 → 3.3 V']),
         ('TH0', 'sensor', 'Hotend thermistor', ['Semitec 104GT-2']),
         ('TB', 'sensor', 'Heatbed thermistor', []),
         ('PWR-DET', 'power', 'Power panic signal', ['from the PSU, active low', '(open = power good)']),
         ('PS-ON', 'lcd', 'Display RESET', ['signal pin (PC13) → RESET of the TFT']),
         ('NEOPIXEL', 'lcd', 'Passive buzzer (KY-006)', ['S → signal (PA8), − → GND'])]
n = len(right)
for i, (name, group, title, lines) in enumerate(right):
    y = BY0 + 70 + i * ((BY1 - BY0 - 110) / (n - 1))
    connector(BX1 - 92, y - 13, 96, 26, name, group)
    by = 100 + i * 80
    callout(BX1 + 4, y, 1430, by, 440, title, lines, group)

# ---------------- left edge ----------------
left = [('EXP1', 'lcd', '4.0" TFT (ST7796S) + encoder', ['jumper wires, one per pin:', 'display SPI + KY-040, see below']),
        ('SPI1', 'lcd', 'Display backlight (LED)', ['SPI1 header pin 3 (PD9)']),
        ('TFT', 'comm', 'MMU2S / MMU3 (optional)', ['TX (PA2) → MMU RX,', 'RX (PA3) ← MMU TX, GND', 'no reset wire needed']),
        ('SWD', 'fan', 'Fan tachometers', ['SWDIO (PA13) ← print fan tach', 'SWCLK (PA14) ← hotend fan tach']),
        ('USB', 'comm', 'USB to PC / Raspberry Pi', ['PrusaSlicer, OctoPrint']),
        ('microSD', 'comm', 'micro-SD card', ['firmware.bin to flash,', 'G-code files to print'])]
n = len(left)
for i, (name, group, title, lines) in enumerate(left):
    y = BY0 + 50 + i * ((BY1 - BY0 - 110) / (n - 1))
    connector(BX0 - 4, y - 14, 92, 28, name, group)
    callout(BX0 - 4, y, 40, 290 + i * 100, 380, title, lines, group)

# ---------------- EXP1 table ----------------
tx, ty = 40, 990
rect(tx, ty, 440, 260, '#ffffff', C['lcd'], 6, 2)
text(tx + 14, ty + 24, 'EXP1 → TFT display + KY-040', 15, weight='bold', fill=C['lcd'])
rows = [('EXP1-1', 'PB5', 'KY-040 CLK'), ('EXP1-2', 'PA15', 'KY-040 SW'), ('EXP1-3', 'PA9', 'KY-040 DT'),
        ('EXP1-4', '—', '(not used)'), ('EXP1-5', 'PA10', 'TFT SDI (MOSI)'), ('EXP1-6', 'PB9', 'TFT DC/RS'),
        ('EXP1-7', 'PB8', 'TFT SCK'), ('EXP1-8', 'PD6', 'TFT CS'), ('EXP1-9', '', 'GND (TFT)'), ('EXP1-10', '', '5 V (TFT VCC)')]
for i, (p, m, s) in enumerate(rows):
    y = ty + 48 + i * 20
    text(tx + 14, y, p, 12.5, weight='bold')
    text(tx + 90, y, m, 12.5, fill='#475569')
    text(tx + 150, y, s, 12.5)
text(tx + 262, ty + 48, 'Pin 1 = PB5 (BTT/Marlin', 11.5, fill='#475569')
text(tx + 262, ty + 64, 'numbering). Check the notch', 11.5, fill='#475569')
text(tx + 262, ty + 80, 'against the silkscreen.', 11.5, fill='#475569')
text(tx + 262, ty + 112, 'TFT SDO/MISO and the', 11.5, fill='#475569')
text(tx + 262, ty + 128, 'touch pins: not used.', 11.5, fill='#475569')
text(tx + 262, ty + 160, 'Ender-3 LCD: -DLCD_ENDER3,', 11.5, fill='#475569')
text(tx + 262, ty + 176, 'MK3S LCD: -DLCD_MK3S,', 11.5, fill='#475569')
text(tx + 262, ty + 192, 'see docs/WIRING.md.', 11.5, fill='#475569')

# ---------------- legend + warnings ----------------
lx, ly = 1430, 1010
rect(lx, ly, 440, 180, '#fff7ed', '#c2410c', 6, 2)
text(lx + 14, ly + 24, 'Before powering up', 15, weight='bold', fill='#c2410c')
warn = ['The STM32 is a 3.3 V part: check that 5 V signals (P.I.N.D.A.,',
        'IR sensor, MMU, PSU power-panic) go to 5 V tolerant pins or',
        'add a divider (10 kΩ series / 20 kΩ to GND).',
        'Fan voltage jumper on 5 V (MK3S fans are 5 V fans).',
        'Flash firmware.bin from the SD card, run the Wizard.',
        'Full pin list: docs/WIRING.md']
for i, wl in enumerate(warn):
    text(lx + 14, ly + 48 + i * 20, wl, 12.5)
lg = [('motor', 'motors'), ('power', 'power / heaters'), ('sensor', 'sensors / thermistors'), ('fan', 'fans'),
      ('lcd', 'display'), ('comm', 'USB / SD / MMU'), ('unused', 'not used')]
x = 480
for g, name in lg:
    rect(x, 1290, 22, 14, C[g], rx=2)
    text(x + 30, 1302, name, 13)
    x += 30 + 7.5 * len(name) + 40
out.append('</svg>')
sys.stdout.write('\n'.join(out))
