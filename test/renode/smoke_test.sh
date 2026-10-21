#!/bin/sh
# Boot the renode_test build in the emulator and check the basic G-code path:
# start-up, M115, settings report, and that the stepper interrupt executes moves
# exactly (M114 "Count" = target).
set -e
cd "$(dirname "$0")"
exec python3 run.py --wait 3 \
  --expect '^start$' \
  --expect 'FIRMWARE_NAME:Prusa-Firmware 3\.14\.1' \
  --expect 'MACHINE_TYPE:Prusa i3 MK3S' \
  --expect 'M92 X100\.00 Y100\.00 Z400\.00 E280\.00' \
  --expect 'X:50\.00 Y:40\.00 Z:12\.00 E:0\.00 Count X: 50\.00 Y:40\.00 Z:12\.00' \
  --expect 'X:0\.50 Y:1\.25 Z:12\.00 E:5\.00 Count X: 0\.50 Y:1\.25 Z:12\.00' \
  "M115" "M503" \
  "G92 X10 Y10 Z10 E0" "G1 X50 Y40 Z12 F3000" "M400" "M114" \
  "G1 X0.5 Y1.25 E5 F1200" "M400" "M114"
