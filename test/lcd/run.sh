#!/bin/sh
# Build and run the host test of the ST7920 display driver
set -e
cd "$(dirname "$0")"
c++ -std=gnu++17 -O2 -Wall -Wextra -DST7920_HOST_TEST -o test_st7920 \
    test_st7920.cpp ../../Firmware/lcd_st7920.cpp
exec ./test_st7920 "$@"
