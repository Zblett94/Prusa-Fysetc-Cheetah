#!/bin/sh
# Heat the simulated hotend to 200 C and the bed to 60 C: PID regulation, thermal
# model (no error with the E3D V6 parameters), fan check, temperature reports.
set -e
cd "$(dirname "$0")"
exec python3 run.py --wait 1 \
  --expect 'T:(199|200|201)\.[0-9] /200\.0 B:(59|60|61)\.[0-9] /60\.0' \
  --forbid 'TM: error' --forbid 'Err|error' --forbid 'fan speed is lower' \
  --expect-lcd '^\| 200/200' \
  "M104 S200" "M140 S60" "@60" "M105" "@20" "M105"
