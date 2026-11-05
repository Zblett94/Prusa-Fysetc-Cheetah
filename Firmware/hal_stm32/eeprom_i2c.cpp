//! @file
//! @brief avr/eeprom.h implementation on the SKR Mini E3 V3 AT24C32 (4 KiB) I2C EEPROM
//!
//! The whole EEPROM is mirrored in RAM at boot, so reads are as fast as on the
//! AVR and safe from any context. Writes are written through to the chip
//! immediately (split at the 32 byte page boundaries), so the data is persistent
//! as soon as the call returns - like on the AVR, which the power panic code
//! relies on. A page write takes <= 5 ms which is faster than writing the same
//! data byte by byte into the ATmega2560 EEPROM (3.3 ms per byte).
//!
//! The bus is bit-banged on PB6 (SCL) / PB7 (SDA) as on the original BTT design.

#if defined(STM32G0xx)
#include <Arduino.h>
#include <string.h>
#include "hal.h"
#include "include/avr/eeprom.h"

#define EEPROM_SIZE      4096
#define EEPROM_PAGE      32
#define EEPROM_I2C_ADDR  0x50
#define EEPROM_SCL_PIN   PB6
#define EEPROM_SDA_PIN   PB7

static uint8_t mirror[EEPROM_SIZE];
static bool eeprom_ok;

//------------------------------------------------------------------------------
// bit-banged I2C (open drain outputs, ~400 kHz)
//------------------------------------------------------------------------------

static inline void i2c_delay() {
	// ~1.25us half period at 64 MHz
	for (volatile uint8_t i = 0; i < 12; i++) {}
}

static inline void scl(bool v) { WRITE_NC(EEPROM_SCL_PIN, v); }
static inline void sda(bool v) { WRITE_NC(EEPROM_SDA_PIN, v); }
static inline bool sda_in() { return READ(EEPROM_SDA_PIN); }
static inline bool scl_in() { return READ(EEPROM_SCL_PIN); }

static void pin_open_drain(uint32_t pin) {
	GPIO_TypeDef *port = _FIO_PORT(pin);
	const uint32_t b = fastio::pin_bit(pin);
	port->BSRR = 1UL << b;
	port->OTYPER |= 1UL << b;
	port->PUPDR = (port->PUPDR & ~(3UL << (b * 2))) | (1UL << (b * 2)); // pull-up (boards have external ones too)
	port->OSPEEDR = (port->OSPEEDR & ~(3UL << (b * 2))) | (1UL << (b * 2));
	port->MODER = (port->MODER & ~(3UL << (b * 2))) | (1UL << (b * 2));
}

static void i2c_wait_scl() {
	// clock stretching
	for (uint16_t i = 0; i < 1000 && !scl_in(); i++) i2c_delay();
}

static void i2c_start() {
	sda(1); scl(1); i2c_wait_scl(); i2c_delay();
	sda(0); i2c_delay();
	scl(0); i2c_delay();
}

static void i2c_stop() {
	sda(0); i2c_delay();
	scl(1); i2c_wait_scl(); i2c_delay();
	sda(1); i2c_delay();
}

static bool i2c_write(uint8_t b) {
	for (uint8_t i = 0; i < 8; i++) {
		sda(b & 0x80); b <<= 1;
		i2c_delay();
		scl(1); i2c_wait_scl(); i2c_delay();
		scl(0);
	}
	sda(1); i2c_delay();
	scl(1); i2c_wait_scl(); i2c_delay();
	const bool ack = !sda_in();
	scl(0); i2c_delay();
	return ack;
}

static uint8_t i2c_read(bool ack) {
	uint8_t b = 0;
	sda(1);
	for (uint8_t i = 0; i < 8; i++) {
		i2c_delay();
		scl(1); i2c_wait_scl(); i2c_delay();
		b = (b << 1) | (sda_in() ? 1 : 0);
		scl(0);
	}
	sda(!ack); i2c_delay();
	scl(1); i2c_wait_scl(); i2c_delay();
	scl(0); sda(1); i2c_delay();
	return b;
}

static void i2c_bus_recover() {
	// clock out a slave stuck in the middle of a read
	sda(1);
	for (uint8_t i = 0; i < 9 && !sda_in(); i++) {
		scl(1); i2c_delay(); i2c_delay();
		scl(0); i2c_delay(); i2c_delay();
	}
	i2c_stop();
}

//! Address the chip at @p addr, polling for the end of a previous write cycle (ACK polling).
static bool eeprom_select(uint16_t addr) {
	// tWR is 5 ms max for the AT24C32, 10 ms for some clones
	for (uint16_t retry = 0; retry < 1000; retry++) {
		i2c_start();
		if (i2c_write(EEPROM_I2C_ADDR << 1)) {
			if (i2c_write(addr >> 8) && i2c_write(addr & 0xFF))
				return true;
		}
		i2c_stop();
		delayMicroseconds(20);
	}
	return false;
}

static bool chip_read(uint16_t addr, uint8_t *dst, uint16_t n) {
	if (!eeprom_select(addr)) return false;
	i2c_start();
	if (!i2c_write((EEPROM_I2C_ADDR << 1) | 1)) { i2c_stop(); return false; }
	while (n--) *dst++ = i2c_read(n != 0);
	i2c_stop();
	return true;
}

static bool chip_write_page(uint16_t addr, const uint8_t *src, uint8_t n) {
	if (!eeprom_select(addr)) return false;
	bool ok = true;
	while (n-- && ok) ok = i2c_write(*src++);
	i2c_stop();
	return ok;
}

//! Write the mirror range [addr, addr+n) to the chip
static void flush_range(uint16_t addr, uint16_t n) {
	if (!eeprom_ok) return;
	hal_uvlo_lock();
	while (n) {
		uint8_t chunk = EEPROM_PAGE - (addr % EEPROM_PAGE);
		if (chunk > n) chunk = n;
		chip_write_page(addr, &mirror[addr], chunk);
		addr += chunk;
		n -= chunk;
	}
	hal_uvlo_unlock();
}

//------------------------------------------------------------------------------
// initialization
//------------------------------------------------------------------------------

//! Read the whole EEPROM into RAM. Returns false if the chip does not respond.
extern "C" bool hal_eeprom_init(void) {
	pin_open_drain(EEPROM_SCL_PIN);
	pin_open_drain(EEPROM_SDA_PIN);
	i2c_bus_recover();
	eeprom_ok = false;
	for (uint8_t attempt = 0; attempt < 3 && !eeprom_ok; attempt++)
		eeprom_ok = chip_read(0, mirror, EEPROM_SIZE);
	if (!eeprom_ok)
		memset(mirror, 0xFF, sizeof(mirror));
	return eeprom_ok;
}

//! Erase the whole EEPROM to 0xFF (the state of a virgin ATmega2560)
extern "C" void hal_eeprom_erase(void) {
	memset(mirror, 0xFF, sizeof(mirror));
	flush_range(0, EEPROM_SIZE);
}

extern "C" bool hal_eeprom_present(void) { return eeprom_ok; }

//------------------------------------------------------------------------------
// avr/eeprom.h API
//------------------------------------------------------------------------------

static inline uint16_t ea(const void *p) { return (uint16_t)((uintptr_t)p & (EEPROM_SIZE - 1)); }

static void read_bytes(void *dst, const void *src, size_t n) {
	uint16_t a = ea(src);
	if (a + n > EEPROM_SIZE) n = EEPROM_SIZE - a;
	memcpy(dst, &mirror[a], n);
}

static void write_bytes(const void *src, void *dst, size_t n, bool update) {
	const uint16_t a = ea(dst);
	if (a + n > EEPROM_SIZE) n = EEPROM_SIZE - a;
	if (update && memcmp(&mirror[a], src, n) == 0) return;
	const uint32_t primask = __get_PRIMASK();
	__disable_irq();
	memcpy(&mirror[a], src, n);
	__set_PRIMASK(primask);
	flush_range(a, n);
}

uint8_t eeprom_read_byte(const uint8_t *p) { return mirror[ea(p)]; }
uint16_t eeprom_read_word(const uint16_t *p) { uint16_t v; read_bytes(&v, p, sizeof(v)); return v; }
uint32_t eeprom_read_dword(const uint32_t *p) { uint32_t v; read_bytes(&v, p, sizeof(v)); return v; }
float eeprom_read_float(const float *p) { float v; read_bytes(&v, p, sizeof(v)); return v; }
void eeprom_read_block(void *dst, const void *src, size_t n) { read_bytes(dst, src, n); }

void eeprom_write_byte(uint8_t *p, uint8_t v) { write_bytes(&v, p, sizeof(v), false); }
void eeprom_write_word(uint16_t *p, uint16_t v) { write_bytes(&v, p, sizeof(v), false); }
void eeprom_write_dword(uint32_t *p, uint32_t v) { write_bytes(&v, p, sizeof(v), false); }
void eeprom_write_float(float *p, float v) { write_bytes(&v, p, sizeof(v), false); }
void eeprom_write_block(const void *src, void *dst, size_t n) { write_bytes(src, dst, n, false); }

void eeprom_update_byte(uint8_t *p, uint8_t v) { write_bytes(&v, p, sizeof(v), true); }
void eeprom_update_word(uint16_t *p, uint16_t v) { write_bytes(&v, p, sizeof(v), true); }
void eeprom_update_dword(uint32_t *p, uint32_t v) { write_bytes(&v, p, sizeof(v), true); }
void eeprom_update_float(float *p, float v) { write_bytes(&v, p, sizeof(v), true); }
void eeprom_update_block(const void *src, void *dst, size_t n) { write_bytes(src, dst, n, true); }

#endif // STM32G0xx
