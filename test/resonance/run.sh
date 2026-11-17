#!/bin/sh
# Host test of the resonance analysis of the input shaper calibration
set -e
cd "$(dirname "$0")"
c++ -std=gnu++17 -O1 -g -Wall -Wextra -o test_resonance test_resonance.cpp -lm
exec ./test_resonance "$@"
