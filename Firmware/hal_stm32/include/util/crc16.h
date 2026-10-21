//! @file
//! @brief util/crc16.h replacement (only the functions used by the firmware).
#pragma once

#include <stdint.h>

static inline uint8_t _crc8_ccitt_update(uint8_t inCrc, uint8_t inData) {
    uint8_t data = inCrc ^ inData;
    for (uint8_t i = 0; i < 8; i++) {
        if ((data & 0x80) != 0) {
            data <<= 1;
            data ^= 0x07;
        } else {
            data <<= 1;
        }
    }
    return data;
}
