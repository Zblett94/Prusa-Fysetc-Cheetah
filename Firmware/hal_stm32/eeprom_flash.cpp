//! @file
//! @brief avr/eeprom.h implementation in the internal flash of the STM32F446 (FYSETC Cheetah V3)
//!
//! The board has no EEPROM chip. The 4 KiB of the ATmega2560 EEPROM are kept in RAM
//! (reads are as fast as on the AVR and safe from any context) and stored in flash
//! sectors 1 and 2 (16 KiB each, 0x08004000..0x0800BFFF, kept free by the linker
//! script). The active sector holds:
//!
//!   0x0000  magic, sequence number (written last: the sector is valid once they are)
//!   0x0008  snapshot of the 4096 bytes
//!   0x1008  log: one 32 bit record per changed byte (address, value, check bits)
//!
//! A write programs one record per changed byte (16 us each, interrupts disabled), so
//! the data is persistent when the call returns, as on the AVR (the power panic code
//! relies on it). When the log is nearly full, the RAM copy is written as a new
//! snapshot into the other sector ("compaction"), which needs a sector erase. An erase
//! stalls the CPU (the code runs from the same flash) for up to 0.5 s, so it is done
//! only when the printer is idle (eeprom_flash_idle(), called from the main loop) or at
//! start-up. If the log ever fills up during a print, further changes stay in RAM
//! until the next compaction.
//!
//! Power loss: a record is either complete or fails its check bits (a Berger code, which
//! detects every partly programmed word); a compaction is valid only after its header is
//! written, and the old sector is erased afterwards.
#if defined(STM32F4xx) || defined(EEPROM_FLASH_HOST_TEST)

#ifdef EEPROM_FLASH_HOST_TEST
// test/eeprom: the flash and the interrupt control are simulated by the test
#include <stdint.h>
#include <stddef.h>
#include <string.h>
#include "../../test/eeprom/host_flash.h"
#else
#include <Arduino.h>
#include <string.h>
#include "hal.h"
#include "include/avr/eeprom.h"
#define EEPROM_FLASH_BASE 0x08004000UL // sectors 1 and 2
#endif

#define EEPROM_SIZE   4096
#define SECTOR_SIZE   0x4000UL
#define SECTOR_ADDR(i) (EEPROM_FLASH_BASE + (i) * SECTOR_SIZE)
#define SECTOR_NUM(i)  (1 + (i))
#define MAGIC         0x45455052UL // "REPE"
#define SNAPSHOT_OFS  8
#define LOG_OFS       (SNAPSHOT_OFS + EEPROM_SIZE)
#define LOG_RECORDS   ((SECTOR_SIZE - LOG_OFS) / 4)
#define LOG_RESERVE   1500 // compact at the next idle moment below this many free records

static uint8_t mirror[EEPROM_SIZE];
static int8_t active = -1;  // sector in use, -1: none yet
static uint32_t sequence;
static uint32_t log_used;   // records in the active sector
static bool pending;        // changes not in flash (log was full)
static bool ready;

//------------------------------------------------------------------------------
// flash access
//------------------------------------------------------------------------------

static inline const volatile uint32_t *word_at(uintptr_t addr) { return (const volatile uint32_t *)addr; }

#ifndef EEPROM_FLASH_HOST_TEST

static void flash_wait() {
	while (FLASH->SR & FLASH_SR_BSY) {}
}

static void flash_unlock() {
	if (FLASH->CR & FLASH_CR_LOCK) {
		FLASH->KEYR = 0x45670123UL;
		FLASH->KEYR = 0xCDEF89ABUL;
	}
	FLASH->SR = FLASH_SR_EOP | FLASH_SR_SOP | FLASH_SR_WRPERR | FLASH_SR_PGAERR | FLASH_SR_PGPERR | FLASH_SR_PGSERR;
}

static void flash_lock() {
	FLASH->CR |= FLASH_CR_LOCK;
}

//! the data cache may hold the content of an erased/programmed sector
static void flash_cache_reset() {
	if (FLASH->ACR & FLASH_ACR_DCEN) {
		FLASH->ACR &= ~FLASH_ACR_DCEN;
		FLASH->ACR |= FLASH_ACR_DCRST;
		FLASH->ACR &= ~FLASH_ACR_DCRST;
		FLASH->ACR |= FLASH_ACR_DCEN;
	}
}

//! program one 32 bit word (interrupts disabled: an interrupt handler may write too)
static bool flash_program(uintptr_t addr, uint32_t value) {
	const uint32_t primask = __get_PRIMASK();
	__disable_irq();
	flash_unlock();
	flash_wait();
	FLASH->CR = FLASH_CR_PSIZE_1 | FLASH_CR_PG; // 32 bit parallelism
	*(volatile uint32_t *)addr = value;
	__DSB();
	flash_wait();
	FLASH->CR &= ~FLASH_CR_PG;
	const bool ok = !(FLASH->SR & (FLASH_SR_SOP | FLASH_SR_WRPERR | FLASH_SR_PGAERR | FLASH_SR_PGPERR | FLASH_SR_PGSERR));
	flash_lock();
	__set_PRIMASK(primask);
	return ok && *word_at(addr) == value;
}

static void flash_erase(uint8_t sector) {
	hal_uvlo_lock(); // no power panic EEPROM writes into a sector being erased
	flash_unlock();
	flash_wait();
	FLASH->CR = FLASH_CR_PSIZE_1 | FLASH_CR_SER | ((uint32_t)SECTOR_NUM(sector) << FLASH_CR_SNB_Pos);
	FLASH->CR |= FLASH_CR_STRT;
	flash_wait();
	FLASH->CR &= ~(FLASH_CR_SER | FLASH_CR_SNB);
	flash_lock();
	flash_cache_reset();
	hal_uvlo_unlock();
}
#endif // !EEPROM_FLASH_HOST_TEST

//------------------------------------------------------------------------------
// records
//------------------------------------------------------------------------------

// A record: bits 0-11 address, 12-19 value, 20-26 hash, 27-31 number of zero bits in
// bits 0-26 (Berger code). An interrupted program leaves some bits at 1 that should be 0:
// the count of zeros then drops while the stored count can only rise, so every partly
// programmed record is detected.
static uint32_t hash7(uint32_t addr, uint32_t value) {
	return (addr ^ (value << 3) ^ (value >> 5) ^ 0x5AUL) & 0x7FUL;
}

static uint32_t zeros27(uint32_t w) {
	return 27 - __builtin_popcount(w & 0x7FFFFFFUL);
}

static uint32_t record(uint16_t addr, uint8_t value) {
	const uint32_t w = (uint32_t)addr | ((uint32_t)value << 12) | (hash7(addr, value) << 20);
	return w | (zeros27(w) << 27);
}

static bool record_valid(uint32_t r) {
	return r != 0xFFFFFFFFUL && (r >> 27) == zeros27(r)
		&& ((r >> 20) & 0x7F) == hash7(r & 0xFFF, (r >> 12) & 0xFF);
}

static bool sector_valid(uint8_t s, uint32_t *seq) {
	if (*word_at(SECTOR_ADDR(s)) != MAGIC) return false;
	*seq = *word_at(SECTOR_ADDR(s) + 4);
	return *seq != 0xFFFFFFFFUL;
}

//! write the RAM copy as a new snapshot into the other sector
static void compact() {
	const int8_t next = active < 0 ? 0 : 1 - active;
	for (uint8_t attempt = 0; attempt < 2; attempt++) {
		flash_erase(next);
		bool ok = true;
		uint8_t snap[EEPROM_SIZE];
		const uint32_t primask = __get_PRIMASK();
		__disable_irq();
		memcpy(snap, mirror, sizeof(snap)); // consistent copy
		pending = false;
		__set_PRIMASK(primask);
		for (uint32_t i = 0; ok && i < EEPROM_SIZE; i += 4) {
			uint32_t w;
			memcpy(&w, &snap[i], 4);
			if (w != 0xFFFFFFFFUL) ok = flash_program(SECTOR_ADDR(next) + SNAPSHOT_OFS + i, w);
		}
		if (ok) ok = flash_program(SECTOR_ADDR(next) + 4, sequence + 1) && flash_program(SECTOR_ADDR(next), MAGIC);
		if (!ok) continue;
		const int8_t old = active;
		active = next;
		sequence++;
		log_used = 0;
		if (old >= 0) flash_erase(old);
		// changes made during the compaction (interrupt handlers) are logged now
		for (uint32_t i = 0; i < EEPROM_SIZE; i++)
			if (mirror[i] != snap[i]) pending = true;
		return;
	}
	pending = true; // flash failure: keep the data in RAM, try again later
}

static void append(uint16_t addr) {
	if (!ready) return;
	if (active < 0 || pending || log_used >= LOG_RECORDS) {
		pending = true;
		return;
	}
	for (;;) {
		// take a log slot: atomic, an interrupt handler may write to the EEPROM as well
		const uint32_t primask = __get_PRIMASK();
		__disable_irq();
		const bool full = active < 0 || log_used >= LOG_RECORDS;
		const uintptr_t a = full ? 0 : SECTOR_ADDR(active) + LOG_OFS + log_used * 4;
		if (!full) log_used++;
		__set_PRIMASK(primask);
		if (full) break;
		if (flash_program(a, record(addr, mirror[addr]))) return;
		// a word that does not program (worn or disturbed) is skipped
	}
	pending = true;
}

//------------------------------------------------------------------------------
// initialization
//------------------------------------------------------------------------------

//! Load the RAM copy from the flash. Returns true when the storage works (a blank
//! flash reads as an erased EEPROM, like a new ATmega2560).
extern "C" bool hal_eeprom_init(void) {
#ifdef EMULATION
	memset(mirror, 0xFF, sizeof(mirror));
	return false; // the test build keeps the settings in RAM (preset by emulation.cpp)
#else
	pending = false;
	uint32_t seq0 = 0, seq1 = 0;
	const bool v0 = sector_valid(0, &seq0), v1 = sector_valid(1, &seq1);
	active = (v0 && v1) ? ((int32_t)(seq1 - seq0) > 0 ? 1 : 0) : v0 ? 0 : v1 ? 1 : -1;
	if (active < 0) {
		// blank (first start): create a sector now, so that the first writes are persistent
		memset(mirror, 0xFF, sizeof(mirror));
		sequence = 0;
		log_used = 0;
		ready = true;
		compact();
	} else {
		sequence = active ? seq1 : seq0;
		memcpy(mirror, (const void *)(SECTOR_ADDR(active) + SNAPSHOT_OFS), EEPROM_SIZE);
		for (log_used = 0; log_used < LOG_RECORDS; log_used++) {
			const uint32_t r = *word_at(SECTOR_ADDR(active) + LOG_OFS + log_used * 4);
			if (r == 0xFFFFFFFFUL) break;
			if (record_valid(r)) mirror[r & 0xFFF] = (r >> 12) & 0xFF;
		}
		// an interrupted compaction left a second valid sector, or the log is nearly full
		if ((v0 && v1) || LOG_RECORDS - log_used < LOG_RESERVE) {
			ready = true;
			compact();
		}
	}
	ready = true;
	return true;
#endif
}

//! Erase the whole EEPROM to 0xFF (the state of a virgin ATmega2560)
extern "C" void hal_eeprom_erase(void) {
	memset(mirror, 0xFF, sizeof(mirror));
#ifndef EMULATION
	compact();
#endif
}

extern "C" bool hal_eeprom_present(void) { return ready; }

//! Called from the main loop when the printer is idle: compact the log when needed.
extern "C" void eeprom_flash_idle(void) {
	if (ready && active >= 0 && (pending || LOG_RECORDS - log_used < LOG_RESERVE))
		compact();
	else if (ready && active < 0 && pending)
		compact();
}

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
	const uint8_t *s = (const uint8_t *)src;
	for (size_t i = 0; i < n; i++) {
		if (mirror[a + i] == s[i]) continue; // also for "write": an unchanged byte needs no record
		(void)update;
		mirror[a + i] = s[i];
		append(a + i);
	}
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

#endif // STM32F4xx || EEPROM_FLASH_HOST_TEST
