#!/bin/sh
# Build and run the host test of the flash EEPROM emulation (Cheetah V3)
set -e
cd "$(dirname "$0")"
c++ -std=gnu++17 -O1 -g -Wall -Wextra -DEEPROM_FLASH_HOST_TEST -o test_eeprom \
    test_eeprom.cpp ../../Firmware/hal_stm32/eeprom_flash.cpp
exec ./test_eeprom "$@"
