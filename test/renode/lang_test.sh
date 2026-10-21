#!/bin/sh
# Check the language tables added to firmware.bin by buildroot/scripts/lang.py:
# the start-up language menu lists the languages, and a translated message
# ("Heating" in German) is shown on the LCD.
set -e
cd "$(dirname "$0")"
python3 run.py --lang 255 --boot 8 \
  --expect-lcd '^\|>English' --expect-lcd '^\| Deutsch'
exec python3 run.py --lang 2 --wait 1 \
  --expect-lcd 'Aufw.rmen' \
  "M104 S35" "M109 S35"
