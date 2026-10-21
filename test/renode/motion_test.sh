#!/bin/sh
# Motion engine at the MK3.5 limits: fast moves with input shaping and pressure advance
# must end exactly at the target (M114 "Count" = position) without step underruns.
set -e
cd "$(dirname "$0")"
exec python3 run.py --wait 1 \
  --expect 'axis X type=MZV freq=50.70' \
  --expect 'axis Y type=MZV freq=40.60' \
  --expect 'Pressure advance: 0.0400' \
  --expect 'X:20.00 Y:20.00 Z:10.00 E:7.20 Count X: 20.00 Y:20.00 Z:10.00 E:7.20' \
  --expect 'axis Y type=MZV freq=30.60' \
  --expect 'bed_weight=400\.0\s+echo:motion: \d+ blocks, 0 planner underruns, 0 step underruns' \
  --forbid 'Error|error' \
  "M593" "M302" "M572 S0.04" "M572" \
  "M201 X7000 Y7000" "M203 X300 Y300" "M204 P7000 T7000" \
  "G92 X100 Y100 Z10 E0" \
  "G1 X200 Y150 F18000" "G1 X20 Y20" \
  "G1 X60 Y20 E1.2 F12000" "G1 X60 Y60 E2.4" "G1 X20 Y60 E3.6" "G1 X20 Y20 E4.8" \
  "G1 X40 Y30 E5.4 F9000" "G1 X30 Y40 E6.0" "G1 X20 Y20 E7.2" \
  "M400" "@1" "M114" "M74 W400" "M593"
