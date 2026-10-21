// Host test of the ST7920 driver (Firmware/lcd_st7920.cpp).
//
// The pins of the driver feed a model of the ST7920 serial interface and graphic RAM.
// HD44780 command sequences like the ones lcd.cpp sends are played, and the picture
// the display would show is compared with an independent rendering of the expected
// 20x4 text. Writes a picture of the screen (PGM) when a file name is given.
#include <cstdio>
#include <cstdlib>
#include <cstring>
#include <cstdint>
#include <string>
#include <vector>
#include "../../Firmware/lcd_st7920.h"
#include "../../Firmware/lcd_st7920_font.h"

static int failures;
#define CHECK(c, ...) do { if (!(c)) { printf("FAIL %s:%d: ", __FILE__, __LINE__); printf(__VA_ARGS__); printf("\n"); failures++; } } while (0)

// ---------------------------------------------------------------- ST7920 model
namespace st {
bool cs, clk, dat;
uint8_t shift, nbits;
int half = -1;          // -1: waiting for a sync byte, 0/1: next byte is the high/low nibble
bool rs;
uint8_t hi;
bool re, graphic, display_on, gaddr_second;
uint8_t gy, gx, gbyte;  // GDRAM address (row 0..31, word 0..15), byte in word
uint16_t gdram[32][16];
long bytes_received, protocol_errors;

void command(uint8_t c) {
	if (c & 0x80) {
		if (!re) { protocol_errors++; return; } // DDRAM address: the driver never uses the text RAM
		if (!gaddr_second) { gy = c & 0x3F; gaddr_second = true; }
		else { gx = c & 0x0F; gaddr_second = false; gbyte = 0; }
	} else if ((c & 0xE0) == 0x20) { // function set
		re = c & 0x04;
		if (re) graphic = c & 0x02;
	} else if (!re && (c & 0xF8) == 0x08) {
		display_on = c & 0x04;
	} else if (!re && c == 0x01) {
		// clear text RAM
	} else {
		protocol_errors++;
	}
}

void data(uint8_t d) {
	if (gaddr_second || gy >= 32) { protocol_errors++; return; }
	uint16_t &w = gdram[gy][gx & 15];
	if (gbyte == 0) { w = (w & 0x00FF) | (d << 8); gbyte = 1; }
	else { w = (w & 0xFF00) | d; gbyte = 0; gx = (gx + 1) & 15; }
}

void byte(uint8_t b) {
	bytes_received++;
	if ((b & 0xF8) == 0xF8 && (b & 0x0F) != 0) { // synchronizing string 11111 RW RS 0
		if (b & 0x04) protocol_errors++;          // RW = read
		rs = b & 0x02;
		half = 0;
		return;
	}
	if (half < 0 || (b & 0x0F)) { protocol_errors++; return; }
	if (half == 0) { hi = b; half = 1; return; }
	const uint8_t v = hi | (b >> 4);
	half = 0;
	rs ? data(v) : command(v);
}

bool pixel(int x, int y) {
	const int row = y & 31, word = (y >= 32 ? 8 : 0) + x / 16;
	return gdram[row][word] & (0x8000 >> (x & 15));
}
} // namespace st

void st7920_io_cs(bool v) {
	if (!v) { st::nbits = 0; st::half = -1; }
	st::cs = v;
}
void st7920_io_clk(bool v) {
	if (v && !st::clk && st::cs) {
		st::shift = (st::shift << 1) | st::dat;
		if (++st::nbits == 8) { st::byte(st::shift); st::nbits = 0; }
	}
	st::clk = v;
}
void st7920_io_dat(bool v) { st::dat = v; }
void st7920_io_delay_us(uint32_t) {}
static bool timer_running;
void st7920_io_timer_kick() { timer_running = true; }

static long run_ticks() {
	long n = 0;
	while (timer_running) {
		if (!st7920_tick()) timer_running = false;
		n++;
	}
	return n;
}

// ---------------------------------------------------------------- HD44780 side (like lcd.cpp)
static void cmd(uint8_t c) { st7920_hd44780_send(c, false); }
static void put(uint8_t c) { st7920_hd44780_send(c, true); }
static const uint8_t row_offsets[] = {0x00, 0x40, 0x14, 0x54};
static void at(uint8_t col, uint8_t row) { cmd(0x80 | (row_offsets[row] + col)); }
static void print(const char *s) { while (*s) put((uint8_t)*s++); }
static void create_char(uint8_t slot, const uint8_t rows[8]) {
	cmd(0x40 | (slot << 3));
	for (int i = 0; i < 8; i++) put(rows[i] | 0xE0); // lcd.cpp leaves garbage in bits 5..7
}

// expected screen
static uint8_t text[4][20];
static uint8_t cg[8][8];
static void expect_clear() { memset(text, ' ', sizeof(text)); }
static void expect(uint8_t col, uint8_t row, const char *s) { while (*s) text[row][col++] = (uint8_t)*s++; }

static bool ref_pixel(int x, int y) {
	if (x < 4 || x >= 124) return false;
	const int col = (x - 4) / 6, cx = (x - 4) % 6, row = y / 16, gy = (y % 16) / 2;
	if (cx == 5) return false;
	const uint8_t c = text[row][col];
	uint8_t bits;
	if (c < 0x10) bits = cg[c & 7][gy];
	else if (c < 0x20) bits = 0;
	else bits = hd44780_rom_a00[c - 0x20][gy];
	return bits & (0x10 >> cx);
}

static void compare(const char *what) {
	int bad = 0, bad_fb = 0;
	for (int y = 0; y < 64; y++)
		for (int x = 0; x < 128; x++) {
			if (st::pixel(x, y) != ref_pixel(x, y)) bad++;
			if (((st7920_fb[y][x / 8] >> (7 - x % 8)) & 1) != ref_pixel(x, y)) bad_fb++;
		}
	CHECK(bad == 0, "%s: %d pixels differ on the display", what, bad);
	CHECK(bad_fb == 0, "%s: %d pixels differ in the frame buffer", what, bad_fb);
	CHECK(st::protocol_errors == 0, "%s: %ld protocol errors", what, st::protocol_errors);
}

static void write_pgm(const char *file) {
	const int S = 4, B = 8, W = 128 * S + 2 * B, H = 64 * S + 2 * B;
	FILE *f = fopen(file, "wb");
	if (!f) return;
	fprintf(f, "P5 %d %d 255\n", W, H);
	for (int y = 0; y < H; y++)
		for (int x = 0; x < W; x++) {
			const int px = (x - B) / S, py = (y - B) / S;
			const bool in = x >= B && y >= B && px < 128 && py < 64;
			fputc(in && st::pixel(px, py) ? 20 : 200, f);
		}
	fclose(f);
}

// Prusa custom characters (FontTable.h): thermometer, degree, bed, folder, clock
static void unpack(const uint8_t p[5], uint8_t out[8]) {
	uint8_t col = p[0];
	for (int i = 0; i < 4; i++) {
		out[2 * i] = ((p[1 + i] >> 4) << 1) | (col & 1); col >>= 1;
		out[2 * i + 1] = ((p[1 + i] & 0x0F) << 1) | (col & 1); col >>= 1;
	}
	for (int i = 0; i < 8; i++) out[i] &= 0x1F;
}

int main(int argc, char **argv) {
	// power up: the firmware sets up the display, then lcd_begin() sends its HD44780 init
	st7920_init();
	CHECK(st::graphic && st::re && st::display_on, "display not in graphic mode after init");
	cmd(0x28); cmd(0x0C); cmd(0x01); cmd(0x06);
	expect_clear();
	long ticks = run_ticks();
	compare("after init");
	printf("init: %ld ticks, %ld bytes\n", ticks, st::bytes_received);

	// status screen with custom characters, like lcdui_print_status_screen()
	const uint8_t packed[5][5] = {
		{0x60, 0x25, 0x55, 0x58, 0x87}, // 0x82 thermometer
		{0x00, 0x69, 0x96, 0x00, 0x00}, // 0x81 degree
		{0x3E, 0x0F, 0xA8, 0xAF, 0x00}, // 0x80 bed
		{0x3C, 0x0E, 0xF8, 0x8F, 0x00}, // 0x85 folder
		{0x1C, 0x07, 0x9A, 0x87, 0x00}, // 0x87 clock
	};
	for (int i = 0; i < 5; i++) {
		unpack(packed[i], cg[i]);
		create_char(i, cg[i]);
	}
	at(0, 0); put(0); print(" 215/215"); put(1); print("  Z   0.20");
	at(0, 1); put(2); print("  60/60 "); put(1); print("  ");
	put(3); print(" 42%");
	at(0, 2); print("\x7e""100%     "); put(4); print(" 1h23m");
	at(0, 3); print("Prusa i3 MK3S OK.");
	expect_clear();
	text[0][0] = 0; expect(1, 0, " 215/215"); text[0][9] = 1; expect(10, 0, "  Z   0.20");
	text[1][0] = 2; expect(1, 1, "  60/60 "); text[1][9] = 1; expect(10, 1, "  "); text[1][12] = 3; expect(13, 1, " 42%");
	expect(0, 2, "\x7e""100%     "); text[2][10] = 4; expect(11, 2, " 1h23m");
	expect(0, 3, "Prusa i3 MK3S OK.");
	long before = st::bytes_received;
	ticks = run_ticks();
	compare("status screen");
	printf("status screen: %ld ticks, %ld bytes\n", ticks, st::bytes_received - before);
	if (argc > 1) write_pgm(argv[1]);

	// a changing temperature: only the changed pixel words are sent
	at(2, 0); print("216");
	expect(2, 0, "216");
	before = st::bytes_received;
	ticks = run_ticks();
	compare("one value changed");
	printf("one value changed: %ld ticks, %ld bytes\n", ticks, st::bytes_received - before);
	CHECK(st::bytes_received - before < 200, "too much traffic for 3 characters");

	// clear + redraw of the same content (menus do that) sends nothing
	cmd(0x01);
	at(0, 3); print("Prusa i3 MK3S OK.");
	for (int r = 0; r < 3; r++) {
		at(0, r);
		for (int c = 0; c < 20; c++) put(text[r][c]);
	}
	before = st::bytes_received;
	run_ticks();
	compare("clear and redraw");
	CHECK(st::bytes_received == before, "clear + same content resent %ld bytes", st::bytes_received - before);

	// a new glyph in a CGRAM slot redraws the cells that use it
	uint8_t arrow[8] = {0x00, 0x04, 0x0E, 0x1F, 0x04, 0x04, 0x04, 0x00};
	memcpy(cg[4], arrow, 8);
	create_char(4, arrow);
	at(0, 0); // lcd.cpp sets the DDRAM address again after a CGRAM write
	run_ticks();
	compare("CGRAM changed");

	// address counter wraps like the HD44780: row 0 continues on row 2, row 3 on row 0
	cmd(0x01);
	expect_clear();
	at(18, 0); print("abcd");
	expect(18, 0, "ab"); expect(0, 2, "cd");
	at(19, 3); print("xy");
	expect(19, 3, "x"); expect(0, 0, "y");
	run_ticks();
	compare("address wrap");

	// the UI writes while the interrupt is sending (random interleaving)
	srand(1);
	cmd(0x01);
	expect_clear();
	for (int i = 0; i < 3000; i++) {
		const int col = rand() % 20, row = rand() % 4;
		const uint8_t c = 0x20 + rand() % 0xE0;
		at(col, row); put(c);
		text[row][col] = c;
		for (int k = rand() % 40; k > 0 && timer_running; k--)
			if (!st7920_tick()) timer_running = false;
	}
	run_ticks();
	compare("interleaved writes");

	// lcd_refresh(): the controller is set up again and the screen resent
	memset(st::gdram, 0x55, sizeof(st::gdram));
	st::re = st::graphic = st::display_on = false;
	st7920_init();
	run_ticks();
	compare("refresh");

	// a refresh in the middle of a transfer
	cmd(0x01); expect_clear();
	print("interrupted");
	expect(0, 0, "interrupted");
	for (int k = 0; k < 5; k++) st7920_tick();
	st7920_init();
	run_ticks();
	compare("refresh during a transfer");

	printf(failures ? "FAILED (%d)\n" : "OK\n", failures);
	return failures != 0;
}
