#!/usr/bin/env python3
"""
Run the firmware test build (pio run -e renode_test) in the Renode emulator.

The host serial port of the test build is USART2. The script sends G-code lines at
given emulated times, collects the serial output and the LCD contents (DDRAM shadow
emu_lcd_ddram kept by the test build) and prints them.

usage: run.py [--renode PATH] [--elf PATH] [--boot SECONDS] [--wait SECONDS] [--lang N]
              [--expect REGEX ...] [--forbid REGEX ...] [--expect-lcd REGEX ...]
              [GCODE | @SECONDS | !MONITOR_COMMAND ...]

The firmware is loaded from firmware.bin (next to the ELF file), which contains the
language tables added after linking.

With --expect / --expect-lcd, the script fails (exit code 1) unless every regular
expression is found in the serial output / on the LCD, or if a --forbid expression
is found in the serial output.
"""
import argparse, os, re, subprocess, sys, tempfile

HERE = os.path.dirname(os.path.abspath(__file__))
ROOT = os.path.abspath(os.path.join(HERE, '..', '..'))

def elf_symbol(elf, name):
    out = subprocess.run(['arm-none-eabi-nm', elf], capture_output=True, text=True).stdout
    for line in out.splitlines():
        parts = line.split()
        if len(parts) == 3 and parts[2] == name:
            return int(parts[0], 16)
    raise SystemExit(f'symbol {name} not found')

def write_screen(path, data, scale=4, border=8):
    # ST7920 frame: 64 rows x 16 bytes, MSB = left pixel; dark pixels on a light background
    w, h = 128 * scale + 2 * border, 64 * scale + 2 * border
    img = bytearray([200]) * (w * h)
    for y in range(64):
        for x in range(128):
            if data[y * 16 + x // 8] & (0x80 >> (x % 8)):
                for dy in range(scale):
                    o = (border + y * scale + dy) * w + border + x * scale
                    img[o:o + scale] = bytes([20]) * scale
    with open(path, 'wb') as f:
        f.write(b'P5 %d %d 255\n' % (w, h) + bytes(img))

# per board: build, platform, load address, CPU speed, factory calibration words
# (TS_CAL1, TS_CAL2, VREFINT_CAL addresses and typical values)
BOARDS = {
    'skr': dict(env='renode_test', repl='stm32g0b1.repl', base=0x08002000, mips=64,
                cal=[(0x1FFF75A8, 1034), (0x1FFF75CA, 1386), (0x1FFF75AA, 1652)]),
    'cheetah': dict(env='renode_cheetah', repl='stm32f446.repl', base=0x08000000, mips=180,
                    # MK3S LCD knob on EXP1/EXP2: (GPIO port, pin) of BTN_ENC, BTN_EN1, BTN_EN2
                    knob=(('gpioPortC', 6), ('gpioPortC', 10), ('gpioPortC', 11)),
                    # P.I.N.D.A. (PA0) low: no metal below the probe (the emulator applies the pull-up;
                    # only a change of the level overrides it)
                    init=['gpioPortA OnGPIO 0 true', 'gpioPortA OnGPIO 0 false'],
                    cal=[(0x1FFF7A2C, 942), (0x1FFF7A2E, 1210), (0x1FFF7A2A, 1520)]),
}

def main():
    ap = argparse.ArgumentParser()
    ap.add_argument('--renode', default=os.environ.get('RENODE', 'renode'))
    ap.add_argument('--board', choices=list(BOARDS), default=os.environ.get('RENODE_BOARD', 'skr'),
                    help='skr: SKR Mini E3 V3 (renode_test), cheetah: Cheetah V3 (renode_cheetah)')
    ap.add_argument('--elf')
    ap.add_argument('--boot', type=float, default=6.0, help='emulated seconds before the first command')
    ap.add_argument('--wait', type=float, default=2.0, help='emulated seconds after each command')
    ap.add_argument('--profile', help='write a collapsed stack profile of the commands to this file')
    ap.add_argument('--lang', type=int, default=0, help='language preset: 0 English, 1.. language tables, 255 ask')
    ap.add_argument('--monitor', action='store_true', help='print the output of the emulator monitor (e.g. of !cpu PC)')
    ap.add_argument('--quantum', default='0.00001',
                    help='CPU/timer synchronization step (s); 0.0001 for long runs: the emulator '
                         'needs about 10x less memory per emulated second (it grows by ~0.1 GB/s at 0.00001)')
    ap.add_argument('--sd-image', help='FAT image for the emulated SD card (Cheetah)')
    ap.add_argument('--timeout', type=float, default=1800, help='wall clock limit of the emulator run (s)')
    ap.add_argument('--expect', action='append', default=[], help='regex that must appear in the serial output')
    ap.add_argument('--forbid', action='append', default=[], help='regex that must not appear in the serial output')
    ap.add_argument('--expect-lcd', action='append', default=[], help='regex that must appear on the LCD')
    ap.add_argument('--screen', help='write the picture shown on the Ender-3 display (PGM) to this file')
    ap.add_argument('gcode', nargs='*',
                    help='G-code lines; @<s>: run <s> emulated seconds; !<cmd>: monitor command; '
                         'boards with a knob: %%click, %%enc+<n> / %%enc-<n> (detents), %%lcd (print the LCD now)')
    a = ap.parse_args()
    board = BOARDS[a.board]
    if not a.elf:
        a.elf = os.path.join(ROOT, '.pio/build/%s/firmware.elf' % board['env'])

    tmp = tempfile.mkdtemp()
    repl = os.path.join(HERE, board['repl'])
    if a.sd_image:  # the SD card model with the given image (a copy of the platform file)
        text = open(repl).read().replace('    capacity: 0x2000000\n',
            '    capacity: %d\n    imageFile: "%s"\n' % (os.path.getsize(a.sd_image), os.path.abspath(a.sd_image)))
        repl = os.path.join(tmp, 'platform.repl')
        open(repl, 'w').write(text)
    uart = os.path.join(tmp, 'uart.txt')
    lcd = elf_symbol(a.elf, 'emu_lcd_ddram')
    lang = elf_symbol(a.elf, 'emu_lang')
    fw_bin = os.path.join(os.path.dirname(a.elf), 'firmware.bin')
    lines = [
        'using sysbus',
        'mach create "skr"',
        f'machine LoadPlatformDescription @{repl}',
        f'sysbus LoadELF @{a.elf}',
        f'sysbus LoadBinary @{fw_bin} {board["base"]:#x}',
        f'sysbus WriteByte {lang:#x} {a.lang}',
        f'cpu VectorTableOffset {board["base"]:#x}',
        # one instruction per clock (the real Cortex-M needs a bit more)
        f'cpu PerformanceInMips {board["mips"]}',
        # fine time synchronization between the CPU and the timers (step timing)
        f'emulation SetGlobalQuantum "{a.quantum}"',
        # typical factory calibration: TS_CAL1, TS_CAL2, VREFINT_CAL
    ] + [f'sysbus WriteWord {addr:#x} {val}' for addr, val in board['cal']] + [
        f'usart2 CreateFileBackend @{uart} true',
        'logLevel 3',
        f'emulation RunFor "{a.boot}"',
    ]
    if a.profile:
        lines.append(f'cpu EnableProfilerCollapsedStack @{os.path.abspath(a.profile)} true')
    knob = board.get('knob')
    def pin(p, level):
        return f'{p[0]} OnGPIO {p[1]} {"true" if level else "false"}'
    if knob:  # released: the inputs have pull-ups on the printer
        i = lines.index(f'emulation RunFor "{a.boot}"')
        lines[i:i] = [pin(p, True) for p in knob]
    # after the boot: the firmware configures the pins (pull-ups) during it
    i = lines.index(f'emulation RunFor "{a.boot}"')
    lines[i + 1:i + 1] = board.get('init', [])
    for g in a.gcode:
        if g == '%lcd':
            lines += ['echo "SNAP:"', f'sysbus ReadBytes {lcd:#x} 128']
            continue
        if g == '%click':
            lines += [pin(knob[0], False), 'emulation RunFor "0.3"', pin(knob[0], True), 'emulation RunFor "0.5"']
            continue
        m = re.match(r'%enc([+-])(\d+)$', g)
        if m:
            # one detent = 4 quadrature transitions; (EN1, EN2) levels, 0 = closed
            seq = [(1, 0), (0, 0), (0, 1), (1, 1)] if m.group(1) == '+' else [(0, 1), (0, 0), (1, 0), (1, 1)]
            for _ in range(int(m.group(2))):
                for e1, e2 in seq:
                    lines += [pin(knob[1], e1), pin(knob[2], e2), 'emulation RunFor "0.004"']
            lines.append('emulation RunFor "0.4"')
            continue
        if g.startswith('@'):  # @<seconds>: just run
            lines.append(f'emulation RunFor "{float(g[1:])}"')
            continue
        if g.startswith('!'):  # !<monitor command>, e.g. !gpioPortC OnGPIO 14 true
            lines.append(g[1:])
            continue
        # the monitor string: single quotes when the G-code has double quotes (M862.3 P "MK3S")
        lines.append(f"usart2 WriteLine '{g}'" if '"' in g else f'usart2 WriteLine "{g}"')
        lines.append(f'emulation RunFor "{a.wait}"')
    if a.profile:
        lines.append('cpu DisableProfiler')
    lines += [f'echo "LCD:"', f'sysbus ReadBytes {lcd:#x} 128']
    if a.screen:
        shown = elf_symbol(a.elf, 'st7920_shown')
        lines += [f'echo "SCREEN:"', f'sysbus ReadBytes {shown:#x} 1024']
    lines.append('quit')
    resc = os.path.join(tmp, 'test.resc')
    open(resc, 'w').write('\n'.join(lines) + '\n')

    r = subprocess.run([a.renode, '--disable-xwt', '--console', '-e', f'include @{resc}'],
                       capture_output=True, text=True, timeout=a.timeout)
    out = r.stdout + r.stderr
    serial = open(uart, errors='replace').read() if os.path.exists(uart) else ''
    if a.monitor:
        print('=== monitor ===')
        print('\n'.join(l for l in out.splitlines() if 'SNAP' not in l and not l.startswith('[') and l.strip()))
    print('=== serial ===')
    print(serial)
    # LCD: DDRAM rows start at 0x00, 0x40, 0x14, 0x54
    m = re.search(r'LCD:(.*)', out, re.S)
    data = [int(x, 16) for x in re.findall(r'0x([0-9A-Fa-f]{2})\b', m.group(1).split('SCREEN:')[0])][:128] if m else []
    if a.screen:
        m2 = re.search(r'SCREEN:(.*)', out, re.S)
        px = [int(x, 16) for x in re.findall(r'0x([0-9A-Fa-f]{2})\b', m2.group(1))][:1024] if m2 else []
        if len(px) == 1024:
            write_screen(a.screen, px)
    def rows(d):
        return ''.join('|' + ''.join(chr(c) if 32 <= c < 127 else '?' if c else ' ' for c in d[off:off + 20]) + '|\n'
                       for off in (0x00, 0x40, 0x14, 0x54))
    for i, blk in enumerate(re.findall(r'SNAP:(.*?)(?=SNAP:|LCD:)', out, re.S)):
        d = [int(x, 16) for x in re.findall(r'0x([0-9A-Fa-f]{2})\b', blk)][:128]
        if len(d) == 128:
            print(f'=== LCD snapshot {i + 1} ===')
            print(rows(d), end='')
    print('=== LCD ===')
    screen = ''
    if len(data) == 128:
        for off in (0x00, 0x40, 0x14, 0x54):
            screen += '|' + ''.join(chr(c) if 32 <= c < 127 else '?' if c else ' ' for c in data[off:off + 20]) + '|\n'
    print(screen, end='')
    failed = False
    errors = [l for l in out.splitlines() if 'abort' in l.lower() or 'HardFault' in l]
    if errors:
        print('=== emulator errors ===')
        print('\n'.join(errors))
        failed = True
    for e in a.expect:
        if not re.search(e, serial, re.M):
            print(f'MISSING: {e}')
            failed = True
    for e in a.forbid:
        if re.search(e, serial, re.M):
            print(f'FOUND: {e}')
            failed = True
    for e in a.expect_lcd:
        if not re.search(e, screen, re.M):
            print(f'MISSING on the LCD: {e}')
            failed = True
    sys.exit(1 if failed else 0)

if __name__ == '__main__':
    main()
