#!/bin/sh
# MK3.5 start G-code over USB (OctoPrint): every line must be answered with "ok".
# G29 A hands over to a G80 put in front of the queue; its "ok" used to be lost, and
# OctoPrint then waited for ever after the bed leveling (Cheetah V3 build).
set -e
cd "$(dirname "$0")"
exec python3 run.py --board cheetah --wait 2 \
  --expect '^echo:G29 A -> G80 X10\.0 Y5\.0 W93\.0 H55\.0\nok$' \
  "M555 X10 Y10 W50 H50" "G29 P1" "G29 P1 X23 Y5 W80 H20 C" "G29 P3.2" "G29 A"
