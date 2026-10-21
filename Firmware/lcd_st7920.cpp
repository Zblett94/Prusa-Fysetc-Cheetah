//! @file
//! @brief Creality Ender-3 display (ST7920 128x64, serial) as a 20x4 HD44780, see lcd_st7920.h
#ifdef ST7920_HOST_TEST
#include <string.h>
#include "lcd_st7920.h"
#define ST7920_CS(v)   st7920_io_cs(v)
#define ST7920_CLK(v)  st7920_io_clk(v)
#define ST7920_DAT(v)  st7920_io_dat(v)
#define ST7920_SPIN()
#define ST7920_DELAY_US(us) st7920_io_delay_us(us)
#define ST7920_KICK()  st7920_io_timer_kick()
#define LCD_ST7920
#else
#include "Configuration.h"
#include "pins.h"
#include <Arduino.h>
#include "fastio.h"
#include "hal_stm32/hal.h"
#include "lcd_st7920.h"
#endif

#ifdef LCD_ST7920
#include "lcd_st7920_font.h"

#ifndef ST7920_HOST_TEST
#define ST7920_CS(v)   WRITE(ST7920_CS_PIN, v)
#define ST7920_CLK(v)  WRITE(ST7920_CLK_PIN, v)
#define ST7920_DAT(v)  WRITE(ST7920_DAT_PIN, v)
//! Half a clock period: 6 loop iterations of 3+ cycles, >= 280 ns at 64 MHz.
//! The ST7920 needs 200 ns (clock high / low) at 5 V, Marlin runs this board and
//! display with about the same timing.
#define ST7920_SPIN()  do { uint32_t n_ = 6; __asm__ volatile(".syntax unified\n1: subs %0, %0, #1\n\tbne 1b\n.syntax divided" : "+l"(n_) :: "cc"); } while (0)
#define ST7920_DELAY_US(us) delayMicroseconds(us)
#define ST7920_KICK()  hal_lcd_timer_kick()
#endif

namespace {

constexpr uint8_t COLS = 20;
constexpr uint8_t X0 = 4;      // left margin (128 - 20 * 6) / 2
constexpr uint8_t CELL_W = 6;  // 5 pixels + 1 space
constexpr uint8_t CELL_H = 16; // 8 font rows, doubled

// HD44780 state
uint8_t ddram[0x80];
uint8_t cgram[64];
uint8_t ac;    // address counter
bool ac_cgram; // the address counter points into the CGRAM

// Graphic RAM: st7920_fb (wanted) and st7920_shown (sent). Bit 7 of byte 0 is the left
// pixel, like the ST7920 GDRAM (16 bit words, MSB left).
volatile uint8_t dirty[64]; // per pixel row: words (bit 0..7) that may differ from 'sent'

// background transfer
enum class Tx : uint8_t { Idle, AddrX, DataFirst, Data };
Tx tx_state;
uint8_t tx_y, tx_byte, tx_end, scan_y;

} // namespace

uint8_t st7920_fb[64][16];
uint8_t st7920_shown[64][16];
#define fb st7920_fb
#define sent st7920_shown

static void send_bits(uint8_t b) {
	for (uint8_t i = 0; i < 8; i++) {
		ST7920_CLK(0);
		ST7920_DAT(b & 0x80);
		ST7920_SPIN();
		ST7920_CLK(1); // data is read on the rising edge
		ST7920_SPIN();
		b <<= 1;
	}
}

static void send_sync(bool data) {
	send_bits(data ? 0xFA : 0xF8); // 11111, RW = 0, RS, 0
}

static void send_byte(uint8_t v) {
	send_bits(v & 0xF0);
	send_bits(v << 4);
}

static void command(uint8_t c, uint16_t wait_us = 100) {
	send_sync(false);
	send_byte(c);
	ST7920_DELAY_US(wait_us);
}

//! the (row, column) on the screen of a DDRAM address, false if not visible
static bool cell_of(uint8_t a, uint8_t &row, uint8_t &col) {
	uint8_t r = 0;
	if (a >= 0x40) { a -= 0x40; r = 1; }
	if (a >= 2 * COLS) return false;
	if (a >= COLS) { a -= COLS; r += 2; }
	row = r;
	col = a;
	return true;
}

static void draw_cell(uint8_t row, uint8_t col, uint8_t code) {
	const uint8_t *glyph;
	static const uint8_t blank[8] = {};
	if (code < 0x10) glyph = cgram + (code & 7) * 8;
	else if (code < 0x20) glyph = blank;
	else glyph = hd44780_rom_a00[code - 0x20];

	const uint8_t x = X0 + col * CELL_W;
	const uint8_t bi = x >> 3;
	const uint8_t shift = 10 - (x & 7);     // 6 pixel field inside a 16 bit window
	const uint16_t mask = 0x3F << shift;
	uint8_t dmask = (1 << (bi >> 1)) | (1 << ((bi + 1) >> 1));
	uint8_t y = row * CELL_H;
	for (uint8_t r = 0; r < 8; r++) {
		const uint16_t bits = (uint16_t)((glyph[r] & 0x1F) << 1) << shift;
		for (uint8_t k = 0; k < 2; k++, y++) {
			const uint16_t w = (fb[y][bi] << 8) | fb[y][bi + 1];
			const uint16_t n = (w & ~mask) | bits;
			if (n != w) {
				fb[y][bi] = n >> 8;
				fb[y][bi + 1] = n;
				dirty[y] |= dmask;
			}
		}
	}
}

static void redraw_address(uint8_t a) {
	uint8_t row, col;
	if (cell_of(a, row, col)) draw_cell(row, col, ddram[a]);
}

static void ddram_write(uint8_t a, uint8_t v) {
	if (ddram[a] == v) return;
	ddram[a] = v;
	redraw_address(a);
}

void st7920_hd44780_send(uint8_t data, bool rs) {
	if (rs) {
		if (ac_cgram) {
			const uint8_t a = ac & 0x3F;
			ac = (a + 1) & 0x3F;
			if (cgram[a] == (data & 0x1F)) return;
			cgram[a] = data & 0x1F;
			const uint8_t ch = a >> 3;
			for (uint8_t i = 0; i < sizeof(ddram); i++)
				if (ddram[i] < 0x10 && (ddram[i] & 7) == ch) redraw_address(i);
		} else {
			ddram_write(ac, data);
			ac++;
			if (ac == 0x28) ac = 0x40;
			else if (ac >= 0x68) ac = 0;
		}
	} else if (data & 0x80) { // set DDRAM address
		ac = data & 0x7F;
		ac_cgram = false;
	} else if (data & 0x40) { // set CGRAM address
		ac = data & 0x3F;
		ac_cgram = true;
	} else if (data == 0x01) { // clear
		for (uint8_t i = 0; i < sizeof(ddram); i++) ddram_write(i, ' ');
		ac = 0;
		ac_cgram = false;
	} else if ((data & 0xFE) == 0x02) { // return home
		ac = 0;
		ac_cgram = false;
	}
	// entry mode (always increment), display control, shift and function set: not used by the UI
	ST7920_KICK();
}

void st7920_init() {
#ifndef ST7920_HOST_TEST
	static bool timer_ready;
	if (!timer_ready) {
		SET_OUTPUT(ST7920_CS_PIN);
		SET_OUTPUT(ST7920_CLK_PIN);
		SET_OUTPUT(ST7920_DAT_PIN);
		hal_lcd_timer_init([] { if (!st7920_tick()) hal_lcd_timer_stop(); });
		timer_ready = true;
	}
	hal_lcd_timer_stop();
#endif
	tx_state = Tx::Idle;
	ST7920_CS(0); // a falling CS resets the serial interface of the display
	ST7920_CLK(1);
	ST7920_DAT(0);
	ST7920_DELAY_US(100);
	ST7920_CS(1);
	command(0x20);         // basic instruction set
	command(0x08);         // display off
	command(0x01, 15000);  // clear the text RAM
	command(0x0C);         // display on, no cursor
	command(0x24);         // extended instruction set
	command(0x26);         // graphic display on
	ST7920_CS(0);
	// resend the whole screen
	for (uint8_t y = 0; y < 64; y++) {
		for (uint8_t i = 0; i < 16; i++) sent[y][i] = ~fb[y][i];
		dirty[y] = 0xFF;
	}
	ST7920_KICK();
}

//! next pixel row with changed words, false if none
static bool next_row() {
	for (uint8_t n = 0; n < 64; n++) {
		const uint8_t y = (scan_y + n) & 63;
		uint8_t m = dirty[y];
		if (!m) continue;
		dirty[y] = 0;
		uint8_t changed = 0;
		for (uint8_t w = 0; w < 8; w++) {
			if ((m & (1 << w)) && (fb[y][2 * w] != sent[y][2 * w] || fb[y][2 * w + 1] != sent[y][2 * w + 1]))
				changed |= 1 << w;
		}
		if (!changed) continue;
		tx_y = y;
		tx_byte = 2 * __builtin_ctz(changed);
		tx_end = 2 * (31 - __builtin_clz(changed)) + 1;
		scan_y = y + 1;
		return true;
	}
	return false;
}

bool st7920_tick() {
	switch (tx_state) {
	case Tx::Idle:
		if (!next_row()) {
			ST7920_CS(0);
			return false;
		}
		ST7920_CS(1);
		send_sync(false);
		send_byte(0x80 | (tx_y & 31)); // vertical address; rows 32..63 are the right half of 256x32
		tx_state = Tx::AddrX;
		return true;
	case Tx::AddrX:
		send_byte(0x80 | (tx_y >= 32 ? 8 : 0) | (tx_byte >> 1)); // horizontal address (16 bit words)
		tx_state = Tx::DataFirst;
		return true;
	case Tx::DataFirst:
		send_sync(true);
		[[fallthrough]];
	case Tx::Data: {
		const uint8_t v = fb[tx_y][tx_byte];
		send_byte(v);
		sent[tx_y][tx_byte] = v;
		tx_state = (tx_byte++ == tx_end) ? Tx::Idle : Tx::Data;
		return true;
	}
	}
	return false;
}

#endif // LCD_ST7920
