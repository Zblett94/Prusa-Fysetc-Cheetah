#!/usr/bin/env python3
"""
Run the firmware test build (pio run -e renode_test) in the Renode emulator.

The host serial port of the test build is USART2. The script sends G-code lines at
given emulated times, collects the serial output and the LCD contents (DDRAM shadow
emu_lcd_ddram kept by the test build) and prints them.

usage: run.py [--renode PATH] [--elf PATH] [--boot SECONDS] [--wait SECONDS] [--lang N]
              [--expect REGEX ...] [--expect-lcd REGEX ...] [GCODE | @SECONDS ...]

The firmware is loaded from firmware.bin (next to the ELF file), which contains the
language tables added after linking.

With --expect / --expect-lcd, the script fails (exit code 1) unless every regular
expression is found in the serial output / on the LCD.
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

def main():
    ap = argparse.ArgumentParser()
    ap.add_argument('--renode', default=os.environ.get('RENODE', 'renode'))
    ap.add_argument('--elf', default=os.path.join(ROOT, '.pio/build/renode_test/firmware.elf'))
    ap.add_argument('--boot', type=float, default=6.0, help='emulated seconds before the first command')
    ap.add_argument('--wait', type=float, default=2.0, help='emulated seconds after each command')
    ap.add_argument('--lang', type=int, default=0, help='language preset: 0 English, 1.. language tables, 255 ask')
    ap.add_argument('--expect', action='append', default=[], help='regex that must appear in the serial output')
    ap.add_argument('--expect-lcd', action='append', default=[], help='regex that must appear on the LCD')
    ap.add_argument('gcode', nargs='*')
    a = ap.parse_args()

    tmp = tempfile.mkdtemp()
    uart = os.path.join(tmp, 'uart.txt')
    lcd = elf_symbol(a.elf, 'emu_lcd_ddram')
    lang = elf_symbol(a.elf, 'emu_lang')
    fw_bin = os.path.join(os.path.dirname(a.elf), 'firmware.bin')
    lines = [
        'using sysbus',
        'mach create "skr"',
        f'machine LoadPlatformDescription @{HERE}/stm32g0b1.repl',
        f'sysbus LoadELF @{a.elf}',
        f'sysbus LoadBinary @{fw_bin} 0x08002000',
        f'sysbus WriteByte {lang:#x} {a.lang}',
        'cpu VectorTableOffset 0x08002000',
        # typical STM32G0B1 factory calibration: TS_CAL1 (30 C), TS_CAL2 (130 C), VREFINT_CAL
        'sysbus WriteWord 0x1FFF75A8 1034',
        'sysbus WriteWord 0x1FFF75CA 1386',
        'sysbus WriteWord 0x1FFF75AA 1652',
        f'usart2 CreateFileBackend @{uart} true',
        'logLevel 3',
        f'emulation RunFor "{a.boot}"',
    ]
    for g in a.gcode:
        if g.startswith('@'):  # @<seconds>: just run
            lines.append(f'emulation RunFor "{float(g[1:])}"')
            continue
        lines.append(f'usart2 WriteLine "{g}"')
        lines.append(f'emulation RunFor "{a.wait}"')
    lines += [f'echo "LCD:"', f'sysbus ReadBytes {lcd:#x} 128', 'quit']
    resc = os.path.join(tmp, 'test.resc')
    open(resc, 'w').write('\n'.join(lines) + '\n')

    r = subprocess.run([a.renode, '--disable-xwt', '--console', '-e', f'include @{resc}'],
                       capture_output=True, text=True, timeout=1800)
    out = r.stdout + r.stderr
    serial = open(uart, errors='replace').read() if os.path.exists(uart) else ''
    print('=== serial ===')
    print(serial)
    # LCD: DDRAM rows start at 0x00, 0x40, 0x14, 0x54
    m = re.search(r'LCD:(.*)', out, re.S)
    data = [int(x, 16) for x in re.findall(r'0x([0-9A-Fa-f]{2})\b', m.group(1))][:128] if m else []
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
    for e in a.expect_lcd:
        if not re.search(e, screen, re.M):
            print(f'MISSING on the LCD: {e}')
            failed = True
    sys.exit(1 if failed else 0)

if __name__ == '__main__':
    main()
