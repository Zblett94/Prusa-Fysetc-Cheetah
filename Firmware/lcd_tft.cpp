//! @file
//! @brief Color TFT (ST7796S 480x320, SPI) showing the Prusa 20x4 UI, see lcd_tft.h
#ifdef TFT_HOST_TEST
#include <string.h>
#include "lcd_tft.h"
#define LCD_TFT
#define TFT_IRQ_SAVE()
#define TFT_IRQ_RESTORE()
#else
#include "Configuration.h"
#include "pins.h"
#include <Arduino.h>
#include "fastio.h"
#include "hal_stm32/hal.h"
#include "lcd_tft.h"
#endif

#ifdef LCD_TFT
#include "tft_fonts.h"

// Display options (see docs/WIRING.md): memory access control (orientation, RGB/BGR)
// and color inversion (IPS panels usually need it).
#ifndef TFT_MADCTL
#define TFT_MADCTL 0x28 // landscape, connector on the left, BGR
#endif
#ifndef TFT_INVERT
#define TFT_INVERT 0
#endif

namespace {

constexpr int W = 480, H = 320;
constexpr int BAND_H = 10, NBANDS = H / BAND_H;
constexpr int COLS = 20, CW = 24, RH = 80; // text mode: 20 x 4 cells of 24 x 80

constexpr uint16_t rgb(uint8_t r, uint8_t g, uint8_t b) {
	return (uint16_t)(((r & 0xF8) << 8) | ((g & 0xFC) << 3) | (b >> 3));
}
constexpr uint16_t C_BG = rgb(0, 0, 0);
constexpr uint16_t C_TEXT = rgb(255, 255, 255);
constexpr uint16_t C_DIM = rgb(150, 150, 150);
constexpr uint16_t C_ACCENT = rgb(250, 104, 49); // Prusa orange
constexpr uint16_t C_SEP = rgb(48, 48, 48);
constexpr uint16_t C_TILE = rgb(32, 32, 32);
constexpr uint16_t C_STATUS = rgb(18, 18, 18);
constexpr uint16_t C_BAR = rgb(70, 70, 70);

struct Rect { int16_t x, y, w, h; };

// status screen tiles
constexpr Rect T_NOZZLE = {6, 6, 231, 108}, T_BED = {243, 6, 231, 108};
constexpr Rect T_Z = {6, 120, 231, 66}, T_SPEED = {243, 120, 231, 66};
constexpr Rect T_PROGRESS = {6, 192, 231, 66}, T_TIME = {243, 192, 231, 66};
constexpr Rect T_STATUS = {0, 264, 480, 56};
constexpr Rect FULL = {0, 0, W, H};

// HD44780 state
uint8_t ddram[0x80];
uint8_t ac;
bool ac_cgram;

enum class Mode : uint8_t { Text, Status };
volatile Mode mode = Mode::Text;
bool status_drawing;

// changed area per band: x range, empty when x1 < x0
int16_t dirty_x0[NBANDS], dirty_x1[NBANDS];
uint8_t scan_band;

// band buffers: pixels in the byte order of the display (RGB565, high byte first)
uint16_t band_buf[2][W * BAND_H];
volatile bool dma_busy;
int8_t ready = -1, sending = -1;
Rect ready_rect;
volatile bool paused = true;
bool initialized;

} // namespace

// ---------------------------------------------------------------- hardware
#ifdef TFT_HOST_TEST
#define hw_reset(a) tft_io_reset(a)
#define hw_cs(a) tft_io_cs(a)
#define hw_dc(d) tft_io_dc(d)
#define hw_write(p, n) tft_io_write(p, n)
#define hw_dma(p, n) tft_io_dma(p, n)
#define hw_wait_idle()
#define hw_delay_ms(ms) tft_io_delay_ms(ms)
#define hw_kick() tft_io_kick()
#else
#define TFT_IRQ_SAVE() const uint32_t primask_ = __get_PRIMASK(); __disable_irq()
#define TFT_IRQ_RESTORE() __set_PRIMASK(primask_)

static void gpio_af(GPIO_TypeDef *port, uint32_t pin, uint32_t af) {
	port->OSPEEDR |= 3UL << (pin * 2);
	port->AFR[pin >> 3] = (port->AFR[pin >> 3] & ~(0xFUL << ((pin & 7) * 4))) | (af << ((pin & 7) * 4));
	port->MODER = (port->MODER & ~(3UL << (pin * 2))) | (2UL << (pin * 2));
}

static void hw_reset(bool active) { WRITE(TFT_RST_PIN, !active); }
static void hw_cs(bool active) { WRITE(TFT_CS_PIN, !active); }
static void hw_dc(bool data) { WRITE(TFT_DC_PIN, data); }

// The loops are bounded: a missing display (or the emulator) must not hang the printer.
static void hw_wait_idle() {
	for (uint32_t i = 0; (SPI2->SR & (SPI_SR_FTLVL | SPI_SR_BSY)) && i < 20000; ++i) {}
}

static void hw_write(const uint8_t *p, uint32_t n) {
	while (n--) {
		for (uint32_t i = 0; !(SPI2->SR & SPI_SR_TXE) && i < 2000; ++i) {}
		*(volatile uint8_t *)&SPI2->DR = *p++;
	}
}

static void hw_dma(const void *p, uint32_t n) {
	DMA1_Channel1->CCR = 0;
	DMA1->IFCR = DMA_IFCR_CGIF1;
	DMA1_Channel1->CMAR = (uint32_t)p;
	DMA1_Channel1->CNDTR = n;
	DMA1_Channel1->CCR = DMA_CCR_MINC | DMA_CCR_DIR | DMA_CCR_TCIE | DMA_CCR_EN;
}

static void hw_delay_ms(uint32_t ms) { delay(ms); }
static void hw_kick() { NVIC_SetPendingIRQ(TIM7_LPTIM2_IRQn); }

static void dma_irq() {
	if (DMA1->ISR & DMA_ISR_TCIF1) {
		DMA1->IFCR = DMA_IFCR_CGIF1;
		DMA1_Channel1->CCR = 0;
		tft_dma_done();
	}
}

static void hw_init() {
	SET_OUTPUT(TFT_CS_PIN);
	SET_OUTPUT(TFT_DC_PIN);
	SET_OUTPUT(TFT_RST_PIN);
	WRITE(TFT_CS_PIN, 1);
#ifdef TFT_BL_PIN
	SET_OUTPUT(TFT_BL_PIN);
	WRITE(TFT_BL_PIN, 1);
#endif
	// SPI2: SCK PB8 (AF1), MOSI PA10 (AF0); master, transmit only, 8 bit, mode 0
	__HAL_RCC_SPI2_CLK_ENABLE();
	gpio_af(GPIOB, 8, 1);
	gpio_af(GPIOA, 10, 0);
	SPI2->CR1 = 0;
	SPI2->CR2 = (7U << SPI_CR2_DS_Pos) | SPI_CR2_FRXTH | SPI_CR2_TXDMAEN;
	SPI2->CR1 = SPI_CR1_MSTR | SPI_CR1_SSM | SPI_CR1_SSI | SPI_CR1_BIDIMODE | SPI_CR1_BIDIOE
		| (TFT_SPI_BR << SPI_CR1_BR_Pos) | SPI_CR1_SPE;
	// DMA1 channel 1 <- DMAMUX request SPI2_TX
	__HAL_RCC_DMA1_CLK_ENABLE();
	DMA1_Channel1->CCR = 0;
	DMA1_Channel1->CPAR = (uint32_t)&SPI2->DR;
	DMAMUX1_Channel0->CCR = DMA_REQUEST_SPI2_TX;
	hal_set_irq_handler(DMA1_Channel1_IRQn, dma_irq);
	NVIC_SetPriority(DMA1_Channel1_IRQn, HAL_PRIO_LCD);
	NVIC_EnableIRQ(DMA1_Channel1_IRQn);
	// the TIM7 interrupt (timer not running) is used as software interrupt for tft_pump()
	hal_set_irq_handler(TIM7_LPTIM2_IRQn, tft_pump);
	NVIC_SetPriority(TIM7_LPTIM2_IRQn, HAL_PRIO_LCD);
	NVIC_EnableIRQ(TIM7_LPTIM2_IRQn);
}
#endif

static void command(uint8_t cmd, const uint8_t *p = nullptr, uint8_t n = 0) {
	hw_wait_idle();
	hw_dc(false);
	hw_write(&cmd, 1);
	if (n) {
		hw_wait_idle();
		hw_dc(true);
		hw_write(p, n);
	}
}

static void command1(uint8_t cmd, uint8_t v) { command(cmd, &v, 1); }

static void set_window(const Rect &r) {
	const uint16_t x1 = r.x + r.w - 1, y1 = r.y + r.h - 1;
	const uint8_t ca[4] = {uint8_t(r.x >> 8), uint8_t(r.x), uint8_t(x1 >> 8), uint8_t(x1)};
	const uint8_t ra[4] = {uint8_t(r.y >> 8), uint8_t(r.y), uint8_t(y1 >> 8), uint8_t(y1)};
	command(0x2A, ca, 4); // column address
	command(0x2B, ra, 4); // row address
	command(0x2C);        // memory write
	hw_wait_idle();
	hw_dc(true);
}

static void display_setup() {
	hw_cs(true);
	command(0x11); // sleep out
	hw_delay_ms(120);
	command1(0xF0, 0xC3); // enable the extension commands
	command1(0xF0, 0x96);
	command1(0x36, TFT_MADCTL);
	command1(0x3A, 0x55); // 16 bit color
	command1(0xB4, 0x01); // 1-dot inversion
	static const uint8_t b6[] = {0x80, 0x02, 0x3B};
	command(0xB6, b6, sizeof(b6));
	static const uint8_t e8[] = {0x40, 0x8A, 0x00, 0x00, 0x29, 0x19, 0xA5, 0x33};
	command(0xE8, e8, sizeof(e8));
	command1(0xC1, 0x06);
	command1(0xC2, 0xA7);
	command1(0xC5, 0x18);
	static const uint8_t e0[] = {0xF0, 0x09, 0x0B, 0x06, 0x04, 0x15, 0x2F, 0x54, 0x42, 0x3C, 0x17, 0x14, 0x18, 0x1B};
	command(0xE0, e0, sizeof(e0)); // gamma
	static const uint8_t e1[] = {0xE0, 0x09, 0x0B, 0x06, 0x04, 0x03, 0x2B, 0x43, 0x42, 0x3B, 0x16, 0x14, 0x17, 0x1B};
	command(0xE1, e1, sizeof(e1));
	command1(0xF0, 0x3C); // disable the extension commands
	command1(0xF0, 0x69);
	command(TFT_INVERT ? 0x21 : 0x20);
	command(0x29); // display on
	hw_wait_idle();
	hw_cs(false);
}

// ---------------------------------------------------------------- changed areas
static void mark(const Rect &r) {
	int x0 = r.x < 0 ? 0 : r.x, x1 = r.x + r.w - 1;
	int y0 = r.y < 0 ? 0 : r.y, y1 = r.y + r.h - 1;
	if (x1 >= W) x1 = W - 1;
	if (y1 >= H) y1 = H - 1;
	if (x1 < x0 || y1 < y0) return;
	TFT_IRQ_SAVE();
	for (int b = y0 / BAND_H; b <= y1 / BAND_H; ++b) {
		if (dirty_x1[b] < dirty_x0[b]) { dirty_x0[b] = x0; dirty_x1[b] = x1; }
		else {
			if (x0 < dirty_x0[b]) dirty_x0[b] = x0;
			if (x1 > dirty_x1[b]) dirty_x1[b] = x1;
		}
	}
	TFT_IRQ_RESTORE();
	hw_kick();
}

static bool next_dirty(Rect &r) {
	for (uint8_t n = 0; n < NBANDS; ++n) {
		const uint8_t b = (scan_band + n) % NBANDS;
		TFT_IRQ_SAVE();
		const int16_t x0 = dirty_x0[b], x1 = dirty_x1[b];
		dirty_x0[b] = W;
		dirty_x1[b] = -1;
		TFT_IRQ_RESTORE();
		if (x1 < x0) continue;
		r = {x0, int16_t(b * BAND_H), int16_t(x1 - x0 + 1), BAND_H};
		scan_band = b + 1;
		return true;
	}
	return false;
}

// ---------------------------------------------------------------- drawing into a band
namespace {
struct Band {
	int16_t x0, y0, w, h;
	uint16_t *px;
};
}

static inline uint16_t swap16(uint16_t c) { return (uint16_t)((c >> 8) | (c << 8)); }

static void fill(Band &b, int x, int y, int w, int h, uint16_t c) {
	int x0 = x > b.x0 ? x : b.x0, x1 = (x + w < b.x0 + b.w) ? x + w : b.x0 + b.w;
	int y0 = y > b.y0 ? y : b.y0, y1 = (y + h < b.y0 + b.h) ? y + h : b.y0 + b.h;
	if (x1 <= x0 || y1 <= y0) return;
	const uint16_t s = swap16(c);
	for (int yy = y0; yy < y1; ++yy) {
		uint16_t *p = b.px + (yy - b.y0) * b.w + (x0 - b.x0);
		for (int n = x1 - x0; n; --n) *p++ = s;
	}
}

//! rectangle with rounded corners (radius 8)
static void fill_round(Band &b, const Rect &r, uint16_t c) {
	static const uint8_t inset[8] = {5, 3, 2, 1, 1, 0, 0, 0};
	for (int yy = (r.y > b.y0 ? r.y : b.y0); yy < r.y + r.h && yy < b.y0 + b.h; ++yy) {
		const int d = (yy - r.y < r.y + r.h - 1 - yy) ? yy - r.y : r.y + r.h - 1 - yy;
		const int in = d < 8 ? inset[d] : 0;
		fill(b, r.x + in, yy, r.w - 2 * in, 1, c);
	}
}

static uint16_t mix(uint16_t a, uint16_t b, uint8_t k) { // a + (b - a) * k / 3
	const int ra = a >> 11, ga = (a >> 5) & 63, ba = a & 31;
	const int rb = b >> 11, gb = (b >> 5) & 63, bb = b & 31;
	return (uint16_t)(((ra + (rb - ra) * k / 3) << 11) | ((ga + (gb - ga) * k / 3) << 5) | (ba + (bb - ba) * k / 3));
}

//! one glyph of a font; (x, y) is the top left corner of its cell
static void glyph(Band &b, const TftFont &f, uint8_t code, int x, int y, uint16_t fg, uint16_t bg) {
	uint8_t i = f.map[code];
	if (i == 0xFF) i = f.map['?'];
	if (i == 0xFF) return;
	const TftGlyph &g = f.glyphs[i];
	const int gx = x + g.x, gy = y + g.y;
	if (gx >= b.x0 + b.w || gx + g.w <= b.x0 || gy >= b.y0 + b.h || gy + g.h <= b.y0) return;
	const uint16_t pal[4] = {0, swap16(mix(bg, fg, 1)), swap16(mix(bg, fg, 2)), swap16(fg)};
	const uint8_t *p = f.data + g.offset;
	int row = 0, col = 0;
	const int bx1 = b.x0 + b.w;
	while (row < g.h) {
		const uint8_t v = *p >> 6;
		int run = (*p++ & 63) + 1;
		while (run) {
			const int seg = run < g.w - col ? run : g.w - col;
			const int yy = gy + row;
			if (v && yy >= b.y0 && yy < b.y0 + b.h) {
				int xa = gx + col, xb = xa + seg;
				if (xa < b.x0) xa = b.x0;
				if (xb > bx1) xb = bx1;
				uint16_t *q = b.px + (yy - b.y0) * b.w + (xa - b.x0);
				for (int n = xb - xa; n > 0; --n) *q++ = pal[v];
			}
			col += seg;
			run -= seg;
			if (col == g.w) {
				col = 0;
				if (gy + ++row >= b.y0 + b.h) return; // below the band
			}
		}
	}
}

static int text(Band &b, const TftFont &f, const char *s, int x, int y, uint16_t fg, uint16_t bg) {
	for (; *s; ++s, x += f.advance) glyph(b, f, (uint8_t)*s, x, y, fg, bg);
	return x;
}

// ---------------------------------------------------------------- the two layouts
static uint8_t cell(uint8_t row, uint8_t col) {
	static const uint8_t offs[4] = {0x00, 0x40, 0x14, 0x54};
	return ddram[offs[row] + col];
}

static bool highlighted(uint8_t row) { return cell(row, 0) == '>'; }

static void render_text(Band &b) {
	const uint8_t row = b.y0 / RH;
	const int ry = row * RH;
	const bool hl = highlighted(row);
	fill(b, b.x0, b.y0, b.w, b.h, C_BG);
	if (hl) fill_round(b, {4, int16_t(ry + 4), W - 8, RH - 8}, C_ACCENT);
	else if (row < 3) fill(b, 16, ry + RH - 1, W - 32, 1, C_SEP);
	const uint16_t bg = hl ? C_ACCENT : C_BG;
	int c0 = (b.x0 - 8) / CW, c1 = (b.x0 + b.w + 8) / CW;
	if (c0 < 0) c0 = 0;
	if (c1 > COLS - 1) c1 = COLS - 1;
	for (int c = c0; c <= c1; ++c) {
		const uint8_t code = cell(row, c);
		if (code == ' ' || (c == 0 && hl)) continue;
		if (code == 0xFF) { // solid block (progress bars)
			fill(b, c * CW, ry + 22, CW, 36, hl ? C_TEXT : C_ACCENT);
			continue;
		}
		glyph(b, tft_font_main, code, c * CW, ry, (code == 0x7E && !hl) ? C_DIM : C_TEXT, bg);
	}
}

//! a field of the status screen: DDRAM cells (row, c0..c1) without the surrounding spaces
static uint8_t field(char *out, uint8_t row, uint8_t c0, uint8_t c1) {
	while (c0 <= c1 && cell(row, c0) == ' ') ++c0;
	while (c1 >= c0 && cell(row, c1) == ' ') --c1;
	uint8_t n = 0;
	for (uint8_t c = c0; c <= c1 && c0 <= c1; ++c) out[n++] = cell(row, c);
	out[n] = 0;
	return n;
}

static void tile_temperature(Band &b, const Rect &t, uint8_t icon, uint8_t row) {
	glyph(b, tft_icons, icon, t.x + 10, t.y + (t.h - 48) / 2, C_ACCENT, C_TILE);
	char s[12];
	field(s, row, 1, 8); // "215/215" + degree
	char *slash = strchr(s, '/');
	if (!slash) return;
	*slash = 0;
	char cur[8];
	strncpy(cur, s, 4);
	cur[4] = 0;
	strcat(cur, "\x81"); // degree
	text(b, tft_font_big, cur, t.x + 66, t.y - 4, C_TEXT, C_TILE);
	*slash = '/';
	text(b, tft_font_main, slash, t.x + 70, t.y + 46, C_DIM, C_TILE);
}

static void tile_value(Band &b, const Rect &t, uint8_t icon, uint8_t row, uint8_t c0, uint8_t c1, int dy = 0) {
	glyph(b, tft_icons, icon, t.x + 10, t.y + (t.h - 48) / 2, C_ACCENT, C_TILE);
	char s[24];
	field(s, row, c0, c1);
	text(b, tft_font_main, s, t.x + 66, t.y - 6 + dy, C_TEXT, C_TILE);
}

static void render_status(Band &b) {
	fill(b, b.x0, b.y0, b.w, b.h, C_BG);
	const Rect *tiles[] = {&T_NOZZLE, &T_BED, &T_Z, &T_SPEED, &T_PROGRESS, &T_TIME};
	for (const Rect *t : tiles) fill_round(b, *t, C_TILE);
	fill(b, T_STATUS.x, T_STATUS.y, T_STATUS.w, T_STATUS.h, C_STATUS);

	tile_temperature(b, T_NOZZLE, TFT_ICON_NOZZLE, 0);
	tile_temperature(b, T_BED, TFT_ICON_BED, 1);
	tile_value(b, T_Z, TFT_ICON_Z, 0, 13, 19);        // "Z  0.15" without the Z
	tile_value(b, T_SPEED, TFT_ICON_SPEED, 1, 13, 19); // feedrate without its symbol
	tile_value(b, T_TIME, TFT_ICON_CLOCK, 2, 13, 19);  // time without the clock symbol
	char s[24];
	if (field(s, 2, 7, 11)) // MMU: "F1", "1>2"
		text(b, tft_font_main, s, T_SPEED.x + T_SPEED.w - 12 - (int)strlen(s) * CW, T_SPEED.y - 6, C_ACCENT, C_TILE);

	// progress: " SD 42%", "---%" or the name of the sheet, with a bar when there is a number
	const Rect &t = T_PROGRESS;
	tile_value(b, t, TFT_ICON_SD, 2, 0, 6, -6);
	const uint8_t n = field(s, 2, 0, 6);
	if (n >= 2 && s[n - 1] == '%' && s[n - 2] >= '0' && s[n - 2] <= '9') {
		int pct = 0, k = n - 2, m = 1;
		while (k >= 0 && s[k] >= '0' && s[k] <= '9') { pct += (s[k] - '0') * m; m *= 10; --k; }
		if (pct > 100) pct = 100;
		const int bx = t.x + 66, bw = t.w - 66 - 14, done = bw * pct / 100;
		fill(b, bx, t.y + 50, done, 6, C_ACCENT);
		fill(b, bx + done, t.y + 50, bw - done, 6, C_BAR);
	}

	field(s, 3, 0, COLS - 1);
	for (uint8_t c = 0; c < COLS; ++c) // keep the column positions (scrolling messages)
		glyph(b, tft_font_main, cell(3, c), c * CW, T_STATUS.y - 12, C_TEXT, C_STATUS);
}

static void render(const Rect &r, uint16_t *px) {
	Band b = {r.x, r.y, r.w, r.h, px};
	if (mode == Mode::Status) render_status(b);
	else render_text(b);
}

//! area of the screen that shows the DDRAM cell (row, col)
static Rect cell_area(uint8_t row, uint8_t col, bool col0_marker_changed) {
	if (mode == Mode::Text) {
		if (col0_marker_changed) return {0, int16_t(row * RH), W, RH};
		return {int16_t(col * CW - 8), int16_t(row * RH), CW + 16, RH};
	}
	switch (row) {
	case 0: return col <= 8 ? T_NOZZLE : col >= 12 ? T_Z : Rect{0, 0, 0, 0};
	case 1: return col <= 8 ? T_BED : col >= 12 ? T_SPEED : Rect{0, 0, 0, 0};
	case 2: return col <= 6 ? T_PROGRESS : col <= 11 ? T_SPEED : T_TIME;
	default: return T_STATUS;
	}
}

static void set_mode(Mode m) {
	if (mode == m) return;
	mode = m;
	mark(FULL);
}

static void ddram_write(uint8_t a, uint8_t v) {
	const uint8_t old = ddram[a];
	if (old == v) return;
	if (mode == Mode::Status && !status_drawing && v != ' ')
		set_mode(Mode::Text); // something else draws over the status screen
	ddram[a] = v;
	const uint8_t row = (a & 0x40 ? 1 : 0) + ((a & 0x3F) >= COLS ? 2 : 0);
	const uint8_t col = (a & 0x3F) % COLS;
	if ((a & 0x3F) >= 2 * COLS) return; // not visible
	mark(cell_area(row, col, col == 0 && ((old == '>') != (v == '>'))));
}

// ---------------------------------------------------------------- interface
void tft_hd44780_send(uint8_t data, bool rs) {
	if (rs) {
		if (ac_cgram) { // not used: the custom characters are drawn directly
			ac = (ac + 1) & 0x3F;
			return;
		}
		ddram_write(ac, data);
		ac++;
		if (ac == 0x28) ac = 0x40;
		else if (ac >= 0x68) ac = 0;
	} else if (data & 0x80) {
		ac = data & 0x7F;
		ac_cgram = false;
	} else if (data & 0x40) {
		ac = data & 0x3F;
		ac_cgram = true;
	} else if (data == 0x01) { // clear
		for (uint8_t i = 0; i < sizeof(ddram); i++) ddram_write(i, ' ');
		ac = 0;
		ac_cgram = false;
	} else if ((data & 0xFE) == 0x02) {
		ac = 0;
		ac_cgram = false;
	}
}

void tft_status_begin() {
	status_drawing = true;
	set_mode(Mode::Status);
}

void tft_status_end() {
	status_drawing = false;
}

void tft_pump() {
	if (paused) return;
	for (;;) {
		if (!dma_busy && ready >= 0) {
			hw_cs(true);
			set_window(ready_rect);
			dma_busy = true;
			sending = ready;
			ready = -1;
			hw_dma((const uint8_t *)band_buf[sending], (uint32_t)ready_rect.w * ready_rect.h * 2);
		}
		if (ready >= 0) return; // both buffers are in use: wait for the DMA
		Rect r;
		if (!next_dirty(r)) {
			if (!dma_busy) {
				hw_wait_idle();
				hw_cs(false);
			}
			return;
		}
		const int8_t buf = (dma_busy && sending == 0) ? 1 : 0;
		render(r, band_buf[buf]);
		ready = buf;
		ready_rect = r;
	}
}

void tft_dma_done() {
	dma_busy = false;
	sending = -1;
	tft_pump();
}

//! stop the background transfer (the display commands are sent from the main loop)
static void pause() {
	paused = true;
	for (uint8_t i = 0; dma_busy && i < 20; ++i) hw_delay_ms(1); // a band takes < 5 ms
	dma_busy = false;
	sending = ready = -1;
}

static void resume() {
	mark(FULL);
	paused = false;
	hw_kick();
}

void tft_init() {
	pause();
#ifndef TFT_HOST_TEST
	hw_init();
#endif
	for (uint8_t i = 0; i < sizeof(ddram); i++) ddram[i] = ' ';
	hw_reset(true);
	hw_delay_ms(10);
	hw_reset(false);
	hw_delay_ms(120);
	hw_cs(true);
	command(0x01); // software reset (also when the reset pin is not connected)
	hw_wait_idle();
	hw_cs(false);
	hw_delay_ms(120);
	display_setup();
	initialized = true;
	resume();
}

void tft_refresh() {
	if (!initialized) {
		tft_init();
		return;
	}
	// after a disturbance the display may be reset (asleep): set it up again, cheaply
	pause();
	hw_cs(true);
	command(0x11); // sleep out (no effect when awake)
	hw_wait_idle();
	hw_cs(false);
	hw_delay_ms(5);
	hw_cs(true);
	command1(0x36, TFT_MADCTL);
	command1(0x3A, 0x55);
	command(TFT_INVERT ? 0x21 : 0x20);
	command(0x29);
	hw_wait_idle();
	hw_cs(false);
	resume();
}

#endif // LCD_TFT
