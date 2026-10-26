#!/bin/sh
# An endstop stops the motion engine at once (the P.I.N.D.A. input triggers during a Z move
# down), the motors continue from where they stopped with the next move. The P.I.N.D.A.
# input has a pull-up (an open input reads as triggered), so it is driven low first.
# The trigger times are fixed: boot long enough for the start-up (display set-up) to end.
set -e
cd "$(dirname "$0")"
exec python3 run.py --boot 8 --wait 0.1 \
  --expect 'X:0\.00 Y:0\.00 Z:2\.00 E:0\.00 Count X: 0\.00 Y:0\.00 Z:[5-8]\.[0-9]{2}' \
  --expect 'X:0\.00 Y:0\.00 Z:4\.00 E:0\.00 Count X: 0\.00 Y:0\.00 Z:[7-9]\.[0-9]{2}' \
  --forbid 'Error|error' \
  "!gpioPortC OnGPIO 14 false" "G92 X0 Y0 Z10 E0" "G1 Z2 F600" "@0.3" "!gpioPortC OnGPIO 14 true" "@0.2" "!gpioPortC OnGPIO 14 false" \
  "M400" "M114" "G1 Z4 F600" "M400" "@0.5" "M114"
