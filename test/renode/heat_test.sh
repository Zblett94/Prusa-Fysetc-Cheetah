#!/bin/sh
# Heat the simulated hotend to 150 C and the bed to 40 C: PID regulation, thermal
# model (no error with the E3D V6 parameters), fan check, temperature reports.
# (Real time: the simulated heaters need about a minute.)
set -e
cd "$(dirname "$0")"
exec python3 run.py --wait 1 \
  --expect 'T:1(4[6-9]|5[0-6])\.[0-9] /150\.0 B:(39|40|41)\.[0-9] /40\.0' \
  --forbid 'TM: error' --forbid 'Err|error' --forbid 'fan speed is lower' \
  --expect-lcd '^\|.1(4[6-9]|5[0-6])/150' \
  "M104 S150" "M140 S40" "@70" "M105" "@10" "M105"
