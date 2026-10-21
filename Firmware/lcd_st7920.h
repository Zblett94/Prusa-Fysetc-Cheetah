//! @file
//! @brief Creality Ender-3 display (ST7920 128x64, serial) as a 20x4 HD44780
//!
//! The Prusa UI is written for a 20x4 character LCD. This driver emulates the
//! HD44780 (DDRAM, CGRAM custom characters, address counter) and draws the
//! characters into the ST7920 graphic RAM: 6x16 pixel cells (the 5x8 HD44780
//! font doubled vertically), 20 columns x 4 rows, 4 pixels margin left and right.
//!
//! lcd.cpp keeps sending HD44780 commands, they cost a few microseconds here. The
//! changed pixels are sent to the display in the background, one byte per tick of
//! a low priority timer interrupt (TIM7), so a full screen takes about 50 ms and
//! never blocks the main loop or the motion.
#pragma once
#include <stdint.h>

//! Initialize the display (blocking, about 20 ms). Also used to revive the display
//! (lcd_refresh): the controller is set up again and the whole screen is resent.
void st7920_init();
//! An HD44780 command (rs = false) or data byte (rs = true)
void st7920_hd44780_send(uint8_t data, bool rs);
//! Send the next byte to the display; returns false when everything is sent.
//! Called from the timer interrupt (or by the host test).
bool st7920_tick();

//! The wanted picture and the picture the display shows (what was sent to it).
//! 64 rows of 128 pixels, bit 7 of byte 0 is the left pixel.
extern uint8_t st7920_fb[64][16];
extern uint8_t st7920_shown[64][16];

#ifdef ST7920_HOST_TEST
// test/lcd: the pins and the timer are provided by the test
void st7920_io_cs(bool v);
void st7920_io_clk(bool v);
void st7920_io_dat(bool v);
void st7920_io_delay_us(uint32_t us);
void st7920_io_timer_kick();
#endif
