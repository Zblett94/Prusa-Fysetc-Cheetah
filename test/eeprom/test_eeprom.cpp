// Host test of the flash EEPROM emulation of the Cheetah V3 port (Firmware/hal_stm32/eeprom_flash.cpp).
//
// The two flash sectors are simulated with NOR flash semantics (programming only clears
// bits, erase sets a whole sector to 0xFF). The test writes random data, "reboots"
// (hal_eeprom_init) and compares with a model, lets the log fill up and compacts, and
// cuts the power at random flash operations: an interrupted word program clears a random
// subset of its bits, an interrupted erase leaves random bytes erased. After a power cut
// every completed write must be there and the interrupted one either old or new.
#include <cstdio>
#include <cstdlib>
#include <cstring>
#include <cstdint>
#include "host_flash.h"

extern "C" bool hal_eeprom_init(void);
extern "C" void hal_eeprom_erase(void);
extern "C" void eeprom_flash_idle(void);
uint8_t eeprom_read_byte(const uint8_t *p);
void eeprom_update_byte(uint8_t *p, uint8_t v);
void eeprom_update_block(const void *src, void *dst, size_t n);

uint8_t host_flash[2 * 0x4000];
static long ops, cut_at = -1;
static const char *cut_op = "";
static long cut_offset;
static long programs, erases;
struct PowerCut {};

static uint32_t rnd32() { return ((uint32_t)rand() << 16) ^ (uint32_t)rand(); }

bool flash_program(uintptr_t addr, uint32_t value) {
	uint32_t old;
	memcpy(&old, (void *)addr, 4);
	if (++ops == cut_at) {
		const uint32_t partial = old & (value | rnd32()); // some of the bits to clear
		memcpy((void *)addr, &partial, 4);
		cut_op = "program"; cut_offset = (long)(addr - (uintptr_t)host_flash);
		throw PowerCut();
	}
	programs++;
	const uint32_t now = old & value; // NOR flash: programming clears bits only
	memcpy((void *)addr, &now, 4);
	return now == value;
}

void flash_erase(uint8_t sector) {
	uint8_t *s = host_flash + sector * 0x4000;
	if (++ops == cut_at) {
		for (int i = 0; i < 0x4000; i++) if (rand() % 3 == 0) s[i] = 0xFF;
		cut_op = "erase"; cut_offset = sector * 0x4000;
		throw PowerCut();
	}
	erases++;
	memset(s, 0xFF, 0x4000);
}

static int failures;
#define CHECK(c, ...) do { if (!(c)) { printf("FAIL %s:%d: ", __FILE__, __LINE__); printf(__VA_ARGS__); printf("\n"); failures++; } } while (0)

static uint8_t model[4096];

static int compare(const char *what) {
	int bad = 0;
	for (int a = 0; a < 4096; a++)
		if (eeprom_read_byte((const uint8_t *)(uintptr_t)a) != model[a]) bad++;
	CHECK(bad == 0, "%s: %d bytes differ", what, bad);
	return bad;
}

static void reboot() { ops = 0; cut_at = -1; hal_eeprom_init(); }

int main(int argc, char **argv) {
	srand(argc > 1 ? atoi(argv[1]) : 3);
	memset(host_flash, 0xFF, sizeof(host_flash));
	memset(model, 0xFF, sizeof(model));
	reboot();
	compare("blank flash");

	// writes are persistent right away
	for (int i = 0; i < 500; i++) {
		const int a = rand() % 4096;
		model[a] = rand();
		eeprom_update_byte((uint8_t *)(uintptr_t)a, model[a]);
	}
	reboot();
	compare("after reboot");

	// many writes: the log fills up, the idle task compacts (also across reboots)
	for (int round = 0; round < 40; round++) {
		for (int i = 0; i < 400; i++) {
			const int a = rand() % 4096;
			model[a] = rand();
			eeprom_update_byte((uint8_t *)(uintptr_t)a, model[a]);
		}
		eeprom_flash_idle(); // the main loop calls it when the printer is idle
		if (round % 5 == 0) reboot();
		compare("long run");
	}

	// more changes than the log holds without an idle moment (e.g. during a print):
	// the rest waits in RAM until the next idle moment, then everything is in flash
	for (int i = 0; i < 4000; i++) {
		const int a = rand() % 4096;
		model[a] = rand();
		eeprom_update_byte((uint8_t *)(uintptr_t)a, model[a]);
	}
	eeprom_flash_idle();
	reboot();
	compare("log overflow, then idle");
	eeprom_flash_idle();
	reboot();
	compare("long run, rebooted");
	printf("%ld word programs, %ld sector erases\n", programs, erases);

	// block writes (settings structures)
	uint8_t block[200];
	for (auto &b : block) b = rand();
	memcpy(&model[1000], block, sizeof(block));
	eeprom_update_block(block, (void *)(uintptr_t)1000, sizeof(block));
	reboot();
	compare("block write");

	// power cuts at random flash operations
	int cuts = 0;
	for (int trial = 0; trial < 3000; trial++) {
		uint8_t before[4096];
		memcpy(before, model, sizeof(model));
		ops = 0;
		cut_at = 1 + rand() % 60;
		cut_op = "";
		int addr = -1;
		uint8_t value = 0;
		bool cut = false;
		try {
			if (rand() % 4 == 0) {
				eeprom_flash_idle(); // compaction when needed
			} else {
				for (int i = 0; i < 20; i++) {
					addr = rand() % 4096;
					value = rand();
					eeprom_update_byte((uint8_t *)(uintptr_t)addr, value);
					model[addr] = value;
					addr = -1;
				}
			}
		} catch (PowerCut &) {
			cut = true;
			cuts++;
		}
		reboot();
		// the interrupted write may or may not be there; everything else must be
		int bad = 0;
		for (int a = 0; a < 4096; a++) {
			const uint8_t v = eeprom_read_byte((const uint8_t *)(uintptr_t)a);
			if (a == addr && cut) {
				if (v != model[a] && v != value) bad++;
				model[a] = v;
			} else if (v != model[a]) {
				if (!bad) printf("  addr %d: expected %02x got %02x (before trial %02x); cut during %s at 0x%lx\n", a, model[a], v, before[a], cut_op, cut_offset);
				bad++;
			}
		}
		CHECK(bad == 0, "power cut trial %d (cut %d): %d bytes wrong", trial, cut, bad);
		if (bad) break;
		(void)before;
	}
	printf("%d power cuts\n", cuts);

	// erase to 0xFF (foreign settings at the first start)
	hal_eeprom_erase();
	memset(model, 0xFF, sizeof(model));
	reboot();
	compare("erase");

	printf(failures ? "FAILED (%d)\n" : "OK\n", failures);
	return failures != 0;
}
