//! @file
//! @brief Color TFT (ST7796S 480x320, SPI) showing the Prusa 20x4 UI in a modern style
//!
//! The Prusa UI writes to a 20x4 HD44780. This driver emulates the HD44780 DDRAM and
//! draws its contents on a 480x320 color display:
//!  - menus and messages: 4 rows of 20 characters in 24x80 pixel cells, anti-aliased
//!    font, the selected menu item ('>' in column 0) as an orange bar, icons for the
//!    Prusa symbols (thermometer, bed, folder, back, clock ...);
//!  - the status screen: a dashboard with tiles (nozzle and bed temperatures in big
//!    digits, Z, speed, MMU slot, progress with a bar, print time) and the status line.
//!    The fields are the ones lcdui_print_status_screen() writes (fixed columns).
//!
//! The Prusa custom characters (0x80.., accented letters and symbols) are drawn
//! directly, without the 8 CGRAM slots of the HD44780.
//!
//! There is no frame buffer (it would need 300 KB). The screen is drawn in bands of
//! 480x10 pixels: a change marks the affected rectangle, a low priority interrupt
//! renders the changed bands into one of two band buffers and sends them with
//! SPI2 + DMA while the next band is rendered.
#pragma once
#include <stdint.h>

void tft_init();          //!< set up the display (blocking, about 0.6 s at the first call)
void tft_refresh();       //!< wake the display up again and redraw everything (lcd_refresh)
//! An HD44780 command (rs = false) or data byte (rs = true). Data bytes 0x80.. are Prusa
//! custom characters (FontTable.h) or HD44780 ROM characters (0xDF, 0xE1, ...).
void tft_hd44780_send(uint8_t data, bool rs);
//! lcdui_print_status_screen() starts / ends drawing: show the dashboard layout
void tft_status_begin();
void tft_status_end();
//! Render and send what changed; called by the interrupts (and by the host test).
void tft_pump();
void tft_dma_done();

#ifdef TFT_HOST_TEST
// test/tft: the SPI bus is provided by the test
void tft_io_reset(bool active);
void tft_io_cs(bool active);
void tft_io_dc(bool data);
void tft_io_write(const uint8_t *p, uint32_t n); //!< blocking
void tft_io_dma(const uint8_t *p, uint32_t n);   //!< completes with tft_dma_done()
void tft_io_delay_ms(uint32_t ms);
void tft_io_kick();                              //!< request tft_pump()
#endif
