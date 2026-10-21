#!/bin/sh
# Build and run the host unit test of the motion engine
set -e
cd "$(dirname "$0")"
c++ -std=gnu++17 -O2 -Wall -Wextra -fsingle-precision-constant -DMOTION_CORE_TEST -o test_motion \
    test_motion.cpp ../../Firmware/motion/motion_core.cpp ../../Firmware/motion/input_shaper.cpp
exec ./test_motion
