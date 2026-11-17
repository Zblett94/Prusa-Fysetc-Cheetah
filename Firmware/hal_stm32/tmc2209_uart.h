//! @file
//! @brief TMC2209 single wire UART access
#pragma once

#include <stdint.h>

void tmc2209_uart_init();
//! Write a 32 bit register. Returns false if the echo was not received (bus not working).
bool tmc2209_uart_write(uint8_t slave, uint8_t reg, uint32_t val);
//! Read a 32 bit register. Returns false (and 0) if the driver did not answer.
bool tmc2209_uart_read(uint8_t slave, uint8_t reg, uint32_t *val);
//! Cheetah V3: true when the replies are captured by DMA (the interrupts stay enabled while a
//! driver answers, so reads are safe during a print). False: bit-banged, interrupts off ~0.4 ms.
bool tmc2209_uart_nonblocking();
//! diagnostics: DMA reads, fallbacks (M913)
void tmc2209_uart_report();
