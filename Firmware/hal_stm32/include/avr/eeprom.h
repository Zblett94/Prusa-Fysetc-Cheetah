//! @file
//! @brief avr/eeprom.h replacement.
//! The SKR Mini E3 V3 carries a 4 KiB AT24C32 I2C EEPROM, which is exactly the
//! size of the ATmega2560 EEPROM. The addresses used by the Prusa firmware are
//! therefore kept 1:1. See hal_stm32/eeprom_i2c.cpp.
#pragma once

#include <stdint.h>
#include <stddef.h>

#define E2END 0x0FFF
#define EEMEM

#ifdef __cplusplus
extern "C" {
#endif

uint8_t eeprom_read_byte(const uint8_t *__p);
uint16_t eeprom_read_word(const uint16_t *__p);
uint32_t eeprom_read_dword(const uint32_t *__p);
float eeprom_read_float(const float *__p);
void eeprom_read_block(void *__dst, const void *__src, size_t __n);

void eeprom_write_byte(uint8_t *__p, uint8_t __value);
void eeprom_write_word(uint16_t *__p, uint16_t __value);
void eeprom_write_dword(uint32_t *__p, uint32_t __value);
void eeprom_write_float(float *__p, float __value);
void eeprom_write_block(const void *__src, void *__dst, size_t __n);

void eeprom_update_byte(uint8_t *__p, uint8_t __value);
void eeprom_update_word(uint16_t *__p, uint16_t __value);
void eeprom_update_dword(uint32_t *__p, uint32_t __value);
void eeprom_update_float(float *__p, float __value);
void eeprom_update_block(const void *__src, void *__dst, size_t __n);

static inline int eeprom_is_ready(void) { return 1; }
#define eeprom_busy_wait() do {} while (0)

#ifdef __cplusplus
}
#endif
