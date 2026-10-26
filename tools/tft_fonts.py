#!/usr/bin/env python3
"""Generate Firmware/tft_fonts.h: anti-aliased fonts and icons of the color TFT UI.

  python3 tools/tft_fonts.py > Firmware/tft_fonts.h

Glyphs are 2 bit (4 levels) bitmaps, cropped to their ink and run-length encoded
(byte = level << 6 | run - 1, row by row). Three fonts:
  main   - the 20x4 character set of the Prusa UI in 24x80 pixel cells: ASCII, the
           Prusa custom characters 0x80.. (accented letters, symbols) and the HD44780
           ROM characters the firmware uses (arrows, degree, a/o/u umlauts, micro)
  big    - digits for the temperatures of the status screen
  icons  - 48x48 icons of the status screen tiles

Fonts: JetBrains Mono Bold (SIL Open Font License 1.1) with DejaVu Sans Mono Bold
(Bitstream Vera license) for missing glyphs. The icons are drawn here.
"""
import os, re, sys
from PIL import Image, ImageDraw, ImageFont

ROOT = os.path.abspath(os.path.join(os.path.dirname(__file__), '..'))
JB = '/usr/share/fonts/truetype/jetbrains-mono/JetBrainsMono-Bold.ttf'
DV = '/usr/share/fonts/truetype/dejavu/DejaVuSansMono-Bold.ttf'
SS = 4  # supersampling of the drawn icons

# ---------------------------------------------------------------- character set
def prusa_custom_chars():
    """code 0x80.. -> unicode, from the comments of Firmware/FontTable.h"""
    chars = {}
    for line in open(os.path.join(ROOT, 'Firmware/FontTable.h'), encoding='utf8'):
        m = re.search(r"index=0x([0-9A-F]{2}), utf8='(.)'", line)
        if m:
            chars[int(m.group(1), 16)] = m.group(2)
    return chars

# HD44780 ROM characters used by the firmware (lang/lib/FontGen.py BUILTIN_CHARS)
ROM_CHARS = {0x7E: '→', 0x7F: '←', 0xDF: '°', 0xE1: 'ä', 0xE4: 'µ', 0xEF: 'ö', 0xF5: 'ü'}

# ---------------------------------------------------------------- rendering helpers
def missing(font, ch):
    # a glyph the font does not have renders like a private use code point
    a = font.getmask(ch)
    b = font.getmask('')
    return a.size == b.size and bytes(a) == bytes(b)

def render_char(ch, size, box_w, box_h, baseline):
    """the character in a box_w x box_h 'L' image, pen at x 0, baseline at y"""
    for path in (JB, DV):
        font = ImageFont.truetype(path, size)
        if not missing(font, ch):
            break
    img = Image.new('L', (box_w + 16, box_h), 0)
    ImageDraw.Draw(img).text((8, baseline), ch, font=font, fill=255, anchor='ls')
    return img, 8  # image and the x of the pen

def quantize(v):
    return 0 if v < 40 else 1 if v < 110 else 2 if v < 190 else 3

def encode(img, pen_x=0):
    """crop to the ink; returns (x, y, w, h, rle bytes) with x relative to the pen"""
    px = img.load()
    w, h = img.size
    q = [[quantize(px[x, y]) for x in range(w)] for y in range(h)]
    ys = [y for y in range(h) if any(q[y])]
    xs = [x for x in range(w) if any(q[y][x] for y in range(h))]
    if not ys:
        return (0, 0, 0, 0, b'')
    x0, x1, y0, y1 = xs[0], xs[-1], ys[0], ys[-1]
    out = bytearray()
    run_v, run_n = None, 0
    for y in range(y0, y1 + 1):
        for x in range(x0, x1 + 1):
            v = q[y][x]
            if v == run_v and run_n < 64:
                run_n += 1
            else:
                if run_v is not None:
                    out.append(run_v << 6 | (run_n - 1))
                run_v, run_n = v, 1
    out.append(run_v << 6 | (run_n - 1))
    return (x0 - pen_x, y0, x1 - x0 + 1, y1 - y0 + 1, bytes(out))

def canvas(w, h):
    img = Image.new('L', (w * SS, h * SS), 0)
    return img, ImageDraw.Draw(img)

def down(img, w, h):
    return img.resize((w, h), Image.LANCZOS)

# ---------------------------------------------------------------- icons
def S(v):
    return v * SS

def W(v):
    return max(1, round(v * SS))

def icon_thermometer(d, w, h, cx, cy, s):
    # bulb + stem
    r = s * 0.22
    d.ellipse([S(cx - r), S(cy + s * 0.5 - 2 * r), S(cx + r), S(cy + s * 0.5)], fill=255)
    sw = s * 0.11
    d.rounded_rectangle([S(cx - sw), S(cy - s * 0.5), S(cx + sw), S(cy + s * 0.3)], radius=S(sw), fill=255)
    for k in range(3):  # scale marks
        y = cy - s * 0.35 + k * s * 0.18
        d.rectangle([S(cx + sw + s * 0.06), S(y), S(cx + sw + s * 0.22), S(y + s * 0.05)], fill=255)

def icon_bed(d, w, h, cx, cy, s):
    # heated plate with heat waves
    d.rounded_rectangle([S(cx - s * 0.5), S(cy + s * 0.22), S(cx + s * 0.5), S(cy + s * 0.38)], radius=S(s * 0.04), fill=255)
    for k in (-1, 0, 1):
        x = cx + k * s * 0.28
        pts = []
        for i in range(13):
            t = i / 12
            pts.append((S(x + s * 0.07 * (1 if i % 4 < 2 else -1) * (0.5 if i % 2 else 1)), S(cy + s * 0.12 - t * s * 0.55)))
        d.line(pts, fill=255, width=W(max(1.5, s * 0.07)), joint='curve')

def icon_uplevel(d, w, h, cx, cy, s):
    # arrow going up and back (return)
    lw = W(s * 0.13)
    d.line([(S(cx + s * 0.35), S(cy + s * 0.4)), (S(cx + s * 0.35), S(cy - s * 0.05)), (S(cx - s * 0.2), S(cy - s * 0.05))], fill=255, width=lw, joint='curve')
    d.polygon([(S(cx - s * 0.45), S(cy - s * 0.05)), (S(cx - s * 0.12), S(cy - s * 0.32)), (S(cx - s * 0.12), S(cy + s * 0.22))], fill=255)

def icon_refresh(d, w, h, cx, cy, s):
    r = s * 0.38
    lw = W(s * 0.12)
    d.arc([S(cx - r), S(cy - r), S(cx + r), S(cy + r)], 200, 340, fill=255, width=lw)
    d.arc([S(cx - r), S(cy - r), S(cx + r), S(cy + r)], 20, 160, fill=255, width=lw)
    d.polygon([(S(cx + r + s * 0.14), S(cy - s * 0.12)), (S(cx + r - s * 0.16), S(cy - s * 0.12)), (S(cx + r), S(cy + s * 0.08))], fill=255)
    d.polygon([(S(cx - r - s * 0.14), S(cy + s * 0.12)), (S(cx - r + s * 0.16), S(cy + s * 0.12)), (S(cx - r), S(cy - s * 0.08))], fill=255)

def icon_folder(d, w, h, cx, cy, s):
    d.rounded_rectangle([S(cx - s * 0.5), S(cy - s * 0.32), S(cx - s * 0.05), S(cy - s * 0.12)], radius=S(s * 0.06), fill=255)
    d.rounded_rectangle([S(cx - s * 0.5), S(cy - s * 0.2), S(cx + s * 0.5), S(cy + s * 0.38)], radius=S(s * 0.07), fill=255)

def icon_clock(d, w, h, cx, cy, s):
    r = s * 0.45
    d.ellipse([S(cx - r), S(cy - r), S(cx + r), S(cy + r)], outline=255, width=W(s * 0.11))
    lw = W(s * 0.1)
    d.line([(S(cx), S(cy - r * 0.6)), (S(cx), S(cy)), (S(cx + r * 0.5), S(cy + r * 0.25))], fill=255, width=lw, joint='curve')

def icon_double_down(d, w, h, cx, cy, s):
    lw = W(s * 0.13)
    for dy in (-0.22, 0.12):
        y = cy + dy * s
        d.line([(S(cx - s * 0.35), S(y)), (S(cx), S(y + s * 0.28)), (S(cx + s * 0.35), S(y))], fill=255, width=lw, joint='curve')

def icon_check(d, w, h, cx, cy, s):
    d.line([(S(cx - s * 0.4), S(cy)), (S(cx - s * 0.1), S(cy + s * 0.3)), (S(cx + s * 0.42), S(cy - s * 0.32))], fill=255, width=W(s * 0.15), joint='curve')

def icon_chevron(d, w, h, cx, cy, s):
    d.line([(S(cx - s * 0.15), S(cy - s * 0.35)), (S(cx + s * 0.2), S(cy)), (S(cx - s * 0.15), S(cy + s * 0.35))], fill=255, width=W(s * 0.14), joint='curve')

def icon_nozzle(d, w, h, cx, cy, s):
    # heater block + nozzle tip
    d.rounded_rectangle([S(cx - s * 0.38), S(cy - s * 0.45), S(cx + s * 0.38), S(cy + s * 0.08)], radius=S(s * 0.06), fill=255)
    d.polygon([(S(cx - s * 0.24), S(cy + s * 0.12)), (S(cx + s * 0.24), S(cy + s * 0.12)), (S(cx + s * 0.07), S(cy + s * 0.42)), (S(cx - s * 0.07), S(cy + s * 0.42))], fill=255)
    d.rectangle([S(cx - s * 0.2), S(cy - s * 0.3), S(cx + s * 0.2), S(cy - s * 0.22)], fill=0)

def icon_z(d, w, h, cx, cy, s):
    lw = W(s * 0.1)
    d.line([(S(cx - s * 0.18), S(cy - s * 0.32)), (S(cx - s * 0.18), S(cy + s * 0.32))], fill=255, width=lw)
    d.polygon([(S(cx - s * 0.38), S(cy - s * 0.2)), (S(cx + s * 0.02), S(cy - s * 0.2)), (S(cx - s * 0.18), S(cy - s * 0.48))], fill=255)
    d.polygon([(S(cx - s * 0.38), S(cy + s * 0.2)), (S(cx + s * 0.02), S(cy + s * 0.2)), (S(cx - s * 0.18), S(cy + s * 0.48))], fill=255)
    font = ImageFont.truetype(JB, S(int(s * 0.62)))
    d.text((S(cx + s * 0.25), S(cy)), 'Z', font=font, fill=255, anchor='mm')

def icon_speed(d, w, h, cx, cy, s):
    r = s * 0.46
    d.arc([S(cx - r), S(cy - r * 0.9), S(cx + r), S(cy + r * 1.1)], 180, 360, fill=255, width=W(s * 0.1))
    d.line([(S(cx), S(cy + r * 0.1)), (S(cx + r * 0.55), S(cy - r * 0.5))], fill=255, width=W(s * 0.11))
    d.ellipse([S(cx - s * 0.09), S(cy + r * 0.1 - s * 0.09), S(cx + s * 0.09), S(cy + r * 0.1 + s * 0.09)], fill=255)

def icon_sd(d, w, h, cx, cy, s):
    x0, x1, y0, y1 = cx - s * 0.32, cx + s * 0.32, cy - s * 0.45, cy + s * 0.45
    d.polygon([(S(x0 + s * 0.2), S(y0)), (S(x1), S(y0)), (S(x1), S(y1)), (S(x0), S(y1)), (S(x0), S(y0 + s * 0.2))], fill=255)
    for k in range(4):
        x = x0 + s * 0.2 + k * s * 0.11
        d.rectangle([S(x), S(y0 + s * 0.07), S(x + s * 0.06), S(y0 + s * 0.25)], fill=0)

ICONS_MAIN = {  # Prusa custom characters drawn as icons (FontTable.h order)
    0x80: icon_bed, 0x82: icon_thermometer, 0x83: icon_uplevel, 0x84: icon_refresh,
    0x85: icon_folder, 0x87: icon_clock, 0x88: icon_double_down, 0x89: icon_check,
    0x7E: icon_chevron,
}
ICONS_TILE = ['nozzle', 'bed', 'z', 'speed', 'sd', 'clock']
ICON_FUNCS = {'nozzle': icon_nozzle, 'bed': icon_bed, 'z': icon_z, 'speed': icon_speed,
              'sd': icon_sd, 'clock': icon_clock}

# ---------------------------------------------------------------- fonts
MAIN = dict(size=40, cell_w=24, box_h=80, baseline=54)
BIG = dict(size=64, box_h=76, baseline=62)
ICON = 48

def build_main():
    chars = {c: chr(c) for c in range(0x20, 0x7E)}
    chars.update({c: u for c, u in prusa_custom_chars().items()})
    chars.update(ROM_CHARS)
    glyphs = {}
    for code, ch in sorted(chars.items()):
        if code in ICONS_MAIN:
            img, d = canvas(MAIN['cell_w'] + 4, MAIN['box_h'])
            ICONS_MAIN[code](d, MAIN['cell_w'] + 4, MAIN['box_h'], 2 + MAIN['cell_w'] / 2, MAIN['baseline'] - 14.5, 26)
            glyphs[code] = encode(down(img, MAIN['cell_w'] + 4, MAIN['box_h']), 2)
        else:
            img, pen = render_char(ch, MAIN['size'], MAIN['cell_w'], MAIN['box_h'], MAIN['baseline'])
            glyphs[code] = encode(img, pen)
    return glyphs

def build_big():
    font = ImageFont.truetype(JB, BIG['size'])
    adv = round(font.getlength('0'))
    glyphs = {}
    for ch in '0123456789/-.?°':
        img, pen = render_char(ch, BIG['size'], adv, BIG['box_h'], BIG['baseline'])
        glyphs[ord(ch) if ch != '°' else 0x81] = encode(img, pen)  # 0x81: Prusa degree character
    return glyphs, adv

def build_icons():
    glyphs = {}
    for i, name in enumerate(ICONS_TILE):
        img, d = canvas(ICON, ICON)
        ICON_FUNCS[name](d, ICON, ICON, ICON / 2, ICON / 2, ICON * 0.82)
        glyphs[i] = encode(down(img, ICON, ICON))
    return glyphs

def emit(name, glyphs, cell_w, box_h, out):
    data = bytearray()
    table = []
    codes = sorted(glyphs)
    for code in codes:
        x, y, w, h, rle = glyphs[code]
        table.append((len(data), x, y, w, h))
        data += rle
    out.append(f'// {name}: {len(codes)} glyphs, {len(data)} bytes')
    out.append(f'static const uint8_t {name}_data[] = {{')
    for i in range(0, len(data), 24):
        out.append('    ' + ', '.join(f'0x{b:02X}' for b in data[i:i + 24]) + ',')
    out.append('};')
    out.append(f'static const TftGlyph {name}_glyphs[] = {{')
    for (off, x, y, w, h), code in zip(table, codes):
        out.append(f'    {{{off}, {x}, {y}, {w}, {h}}}, // 0x{code:02X}')
    out.append('};')
    m = [0xFF] * 256
    for i, code in enumerate(codes):
        m[code] = i
    out.append(f'static const uint8_t {name}_map[256] = {{')
    for i in range(0, 256, 16):
        out.append('    ' + ', '.join(f'{v}' for v in m[i:i + 16]) + ',')
    out.append('};')
    out.append(f'static const TftFont {name} = {{{name}_data, {name}_glyphs, {name}_map, {cell_w}, {box_h}}};')
    out.append('')
    return len(data) + 6 * len(codes) + 256

def main():
    out = ['// Generated by tools/tft_fonts.py, do not edit.',
           '// Glyphs: JetBrains Mono Bold (c) The JetBrains Mono Project Authors, SIL Open Font',
           '// License 1.1; DejaVu Sans Mono Bold (c) Bitstream, Inc. / DejaVu, Bitstream Vera',
           '// license. Icons drawn by the script.',
           '#pragma once',
           '#include <stdint.h>',
           '',
           '//! glyph: offset of its RLE data, position of the ink in the cell, size',
           'struct TftGlyph { uint16_t offset; int8_t x, y; uint8_t w, h; };',
           'struct TftFont { const uint8_t *data; const TftGlyph *glyphs; const uint8_t *map; uint8_t advance, box_h; };',
           '']
    total = emit('tft_font_main', build_main(), MAIN['cell_w'], MAIN['box_h'], out)
    big, adv = build_big()
    total += emit('tft_font_big', big, adv, BIG['box_h'], out)
    total += emit('tft_icons', build_icons(), ICON, ICON, out)
    out.append('enum TftIcon : uint8_t { ' + ', '.join(f'TFT_ICON_{n.upper()}' for n in ICONS_TILE) + ' };')
    sys.stdout.write('\n'.join(out) + '\n')
    sys.stderr.write(f'{total} bytes of flash\n')

if __name__ == '__main__':
    main()
