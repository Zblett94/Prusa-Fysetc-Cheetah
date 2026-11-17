#!/bin/sh
# Host test of the TMC2209 reply decoder (Cheetah V3)
set -e
cd "$(dirname "$0")"
c++ -std=gnu++17 -O1 -g -Wall -Wextra -o test_tmcuart test_tmcuart.cpp
exec ./test_tmcuart "$@"
