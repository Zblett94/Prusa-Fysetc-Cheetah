#!/bin/sh
# Build and run the host test of the color TFT driver
set -e
cd "$(dirname "$0")"
c++ -std=gnu++17 -O2 -Wall -Wextra -DTFT_HOST_TEST -o test_tft \
    test_tft.cpp ../../Firmware/lcd_tft.cpp
exec ./test_tft "$@"
