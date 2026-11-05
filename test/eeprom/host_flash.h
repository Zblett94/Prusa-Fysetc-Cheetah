// Simulated flash for the host test of Firmware/hal_stm32/eeprom_flash.cpp (test_eeprom.cpp)
#pragma once
#include <stdint.h>
#include <stddef.h>

extern uint8_t host_flash[2 * 0x4000];
#define EEPROM_FLASH_BASE ((uintptr_t)host_flash)

static inline uint32_t __get_PRIMASK() { return 0; }
static inline void __disable_irq() {}
static inline void __set_PRIMASK(uint32_t) {}

bool flash_program(uintptr_t addr, uint32_t value);
void flash_erase(uint8_t sector);
