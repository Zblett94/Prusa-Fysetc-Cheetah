// Host test of the color TFT driver (Firmware/lcd_tft.cpp).
//
// The SPI bus of the driver feeds a model of the ST7796S (commands, address window,
// 16 bit pixels) with a 480x320 frame buffer. HD44780 sequences like the ones of the
// Prusa UI are played (menus, status screen, translated text) and checked:
//  - the protocol: pixels only inside the address window, exactly the window size;
//  - the dirty tracking: after any sequence of changes, interleaved with the background
//    transfer, the screen equals a complete redraw of the same content;
//  - a few pixels of the layout (highlight bar, tiles, text).
// With a directory argument, screenshots (PPM) are written there.
#include <cstdio>
#include <cstdlib>
#include <cstring>
#include <cstdint>
#include <string>
#include <vector>
#include "../../Firmware/lcd_tft.h"

static int failures;
#define CHECK(c, ...) do { if (!(c)) { printf("FAIL %s:%d: ", __FILE__, __LINE__); printf(__VA_ARGS__); printf("\n"); failures++; } } while (0)

// ---------------------------------------------------------------- ST7796S model
namespace st {
constexpr int W = 480, H = 320;
uint16_t fb[H][W];
bool cs, dc;
uint8_t cmd;
std::vector<uint8_t> args;
int xs, xe, ys, ye, x, y;
bool writing;
uint8_t hi;
bool have_hi;
bool awake, on;
long pixels, window_overflow, protocol_errors, bytes;

void command(uint8_t c) {
	cmd = c;
	args.clear();
	writing = (c == 0x2C);
	have_hi = false;
	if (writing) { x = xs; y = ys; }
	if (c == 0x11) awake = true;
	if (c == 0x29) on = true;
	if (c == 0x01) { awake = false; on = false; }
}

void data(uint8_t d) {
	if (writing) {
		if (!have_hi) { hi = d; have_hi = true; return; }
		have_hi = false;
		if (y > ye) { window_overflow++; return; }
		fb[y][x] = (uint16_t)(hi << 8 | d);
		pixels++;
		if (++x > xe) { x = xs; ++y; }
		return;
	}
	args.push_back(d);
	if (cmd == 0x2A && args.size() == 4) { xs = args[0] << 8 | args[1]; xe = args[2] << 8 | args[3]; }
	if (cmd == 0x2B && args.size() == 4) { ys = args[0] << 8 | args[1]; ye = args[2] << 8 | args[3]; }
	if ((cmd == 0x2A || cmd == 0x2B) && args.size() == 4 && (xs > xe || xe >= W || ys > ye || ye >= H)) protocol_errors++;
}

void byte(uint8_t b) {
	bytes++;
	if (!cs) { protocol_errors++; return; }
	dc ? data(b) : command(b);
}
} // namespace st

void tft_io_reset(bool) {}
void tft_io_cs(bool a) { st::cs = a; }
void tft_io_dc(bool d) { st::dc = d; }
void tft_io_write(const uint8_t *p, uint32_t n) { while (n--) st::byte(*p++); }
static bool dma_pending, kick_pending;
void tft_io_dma(const uint8_t *p, uint32_t n) {
	if (!st::dc || !st::writing) st::protocol_errors++;
	const long before = st::pixels;
	tft_io_write(p, n);
	if (st::pixels - before != (long)n / 2) st::protocol_errors++;
	dma_pending = true;
}
void tft_io_delay_ms(uint32_t) {
	if (dma_pending) { dma_pending = false; tft_dma_done(); }
}
void tft_io_kick() { kick_pending = true; }

//! let the background transfer finish
static void run() {
	while (dma_pending || kick_pending) {
		if (dma_pending) { dma_pending = false; tft_dma_done(); }
		else { kick_pending = false; tft_pump(); }
	}
}

//! a few steps of the background transfer
static void step(int n) {
	while (n-- > 0 && (dma_pending || kick_pending)) {
		if (dma_pending) { dma_pending = false; tft_dma_done(); }
		else { kick_pending = false; tft_pump(); }
	}
}

// ---------------------------------------------------------------- HD44780 side (like lcd.cpp)
static void cmd(uint8_t c) { tft_hd44780_send(c, false); }
static void put(uint8_t c) { tft_hd44780_send(c, true); }
static const uint8_t row_offsets[] = {0x00, 0x40, 0x14, 0x54};
static void at(uint8_t col, uint8_t row) { cmd(0x80 | (row_offsets[row] + col)); }
static void print(const char *s) { while (*s) put((uint8_t)*s++); }
static void print_pad(const char *s, int n) { int k = 0; for (; s[k] && k < n; ++k) put((uint8_t)s[k]); for (; k < n; ++k) put(' '); }
static void clear() { cmd(0x01); }

static void menu(const char *const items[4], const uint8_t type[4], int selected) {
	clear();
	for (int r = 0; r < 4; ++r) {
		at(0, r);
		put(r == selected ? '>' : ' ');
		print_pad(items[r], 18);
		put(type[r]);
	}
}

// status screen exactly as lcdui_print_status_screen() writes it
struct Status {
	int t_noz, t_noz_target, t_bed, t_bed_target, feed;
	float z;
	const char *progress, *mmu, *time, *line;
};

static void status_screen(const Status &s) {
	char b[32];
	tft_status_begin();
	at(0, 0);
	put(0x82);
	int n = snprintf(b, sizeof(b), "%3d/%d", s.t_noz, s.t_noz_target);
	print(b); put(0x81); for (int k = n + 1; k < 8; ++k) put(' ');
	print("   ");
	snprintf(b, sizeof(b), "Z%6.2f ", s.z);
	print(b);
	at(0, 1);
	put(0x80);
	n = snprintf(b, sizeof(b), "%3d/%d", s.t_bed, s.t_bed_target);
	print(b); put(0x81); for (int k = n + 1; k < 8; ++k) put(' ');
	print("   ");
	put(0x86);
	n = snprintf(b, sizeof(b), "%3d%%", s.feed);
	print(b); for (int k = n + 1; k < 8; ++k) put(' ');
	at(0, 2);
	print_pad(s.progress, 7);
	print_pad(s.mmu, 5);
	put(0x87);
	print_pad(s.time, 7);
	at(0, 3);
	print_pad(s.line, 20);
	tft_status_end();
}

// ---------------------------------------------------------------- checks
static uint16_t shot[st::H][st::W];

//! the screen must not change when everything is drawn again
static void check_complete(const char *what) {
	run();
	memcpy(shot, st::fb, sizeof(shot));
	memset(st::fb, 0x55, sizeof(st::fb));
	tft_refresh();
	run();
	int bad = 0, first_x = -1, first_y = -1;
	for (int y = 0; y < st::H; ++y)
		for (int x = 0; x < st::W; ++x)
			if (shot[y][x] != st::fb[y][x]) { if (!bad) { first_x = x; first_y = y; } bad++; }
	CHECK(bad == 0, "%s: %d pixels differ from a complete redraw (first at %d,%d)", what, bad, first_x, first_y);
	CHECK(st::protocol_errors == 0 && st::window_overflow == 0, "%s: protocol errors %ld, overflow %ld", what, st::protocol_errors, st::window_overflow);
}

static void write_ppm(const std::string &dir, const char *name) {
	if (dir.empty()) return;
	FILE *f = fopen((dir + "/" + name + ".ppm").c_str(), "wb");
	if (!f) return;
	fprintf(f, "P6 %d %d 255\n", st::W, st::H);
	for (int y = 0; y < st::H; ++y)
		for (int x = 0; x < st::W; ++x) {
			const uint16_t c = st::fb[y][x];
			const uint8_t rgb[3] = {uint8_t((c >> 11) << 3 | (c >> 13)), uint8_t(((c >> 5) & 63) << 2 | ((c >> 9) & 3)), uint8_t((c & 31) << 3 | ((c >> 2) & 7))};
			fwrite(rgb, 1, 3, f);
		}
	fclose(f);
}

constexpr uint16_t ORANGE = (250 & 0xF8) << 8 | (104 & 0xFC) << 3 | 49 >> 3;

int main(int argc, char **argv) {
	const std::string dir = argc > 1 ? argv[1] : "";
	tft_init();
	CHECK(st::awake && st::on, "display not on after init");
	cmd(0x28); cmd(0x0C); clear(); cmd(0x06);
	run();
	CHECK(st::fb[100][100] == 0, "background not black");

	// status screen
	Status idle = {25, 0, 24, 0, 100, 0.15f, "   ---%", "", "--:--", "Prusa i3 MK3S OK."};
	status_screen(idle);
	check_complete("status idle");
	write_ppm(dir, "status_idle");

	Status printing = {215, 215, 60, 60, 100, 2.35f, " SD 42%", " F2", "01:23R", "benchy_0.2mm_PLA.gco"};
	status_screen(printing);
	run();
	CHECK(st::fb[192 + 53][6 + 66 + 10] == ORANGE, "progress bar missing");
	check_complete("status printing");
	write_ppm(dir, "status_printing");

	// a temperature changes: only the nozzle tile is sent
	long before = st::pixels;
	printing.t_noz = 214;
	status_screen(printing);
	run();
	CHECK(st::pixels - before <= 231L * 120, // the tile spans 12 bands of 10 rows
		 "too many pixels for one temperature: %ld", st::pixels - before);
	check_complete("temperature changed");

	// main menu, second item selected
	const char *main_items[4] = {"Info screen", "Preheat", "Print from SD", "Settings"};
	const uint8_t types[4] = {0x83, 0x7E, 0x7E, 0x7E};
	menu(main_items, types, 1);
	run();
	CHECK(st::fb[80 + 40][440] == ORANGE, "selection bar missing");
	CHECK(st::fb[40][440] == 0, "unselected row not black");
	check_complete("main menu");
	write_ppm(dir, "menu_main");

	// moving the selection redraws two rows
	before = st::pixels;
	at(0, 1); put(' ');
	at(0, 2); put('>');
	run();
	CHECK(st::pixels - before <= 2L * 480 * 80, "selection move sent %ld pixels", st::pixels - before);
	CHECK(st::fb[160 + 40][440] == ORANGE && st::fb[80 + 40][440] == 0, "selection did not move");
	check_complete("selection moved");

	// Czech menu with accented characters (Prusa custom characters 0x80..)
	const char *cz[4] = {"Informace", "P\xB5" "edeh\xB5" "ev", "Tisk z SD", "Nastaven\xA2"};
	menu(cz, types, 3);
	check_complete("Czech menu");
	write_ppm(dir, "menu_czech");

	// a message over the status screen switches to the text layout
	status_screen(idle);
	run();
	at(0, 1); print("Heating bed...      ");
	check_complete("message over status");
	CHECK(st::fb[40][470] == 0, "status tiles still shown under a message");

	// progress bar of solid blocks (menu_progressbar), value edit screen
	clear();
	at(0, 0); print("Live adjust Z");
	at(0, 1); print("    -0.725 mm");
	at(0, 3); for (int k = 0; k < 12; ++k) put(0xFF);
	check_complete("edit screen");
	write_ppm(dir, "edit");

	// random writes while the background transfer runs, mode switches included
	srand(7);
	for (int i = 0; i < 4000; ++i) {
		const int k = rand() % 100;
		if (k < 2) status_screen(rand() % 2 ? idle : printing);
		else if (k < 3) clear();
		else {
			at(rand() % 20, rand() % 4);
			const int c = rand() % 6;
			put(c == 0 ? '>' : c == 1 ? 0x80 + rand() % 74 : 0x20 + rand() % 95);
		}
		step(rand() % 30);
	}
	check_complete("random writes");

	// refresh in the middle of a transfer
	menu(main_items, types, 0);
	step(5);
	tft_refresh();
	check_complete("refresh during a transfer");

	printf("%ld bytes, %ld pixels sent\n", st::bytes, st::pixels);
	printf(failures ? "FAILED (%d)\n" : "OK\n", failures);
	return failures != 0;
}
