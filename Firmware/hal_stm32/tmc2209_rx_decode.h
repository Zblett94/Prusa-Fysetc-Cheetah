//! @file
//! @brief Decoder of the TMC2209 reply, captured by DMA at 8 x the baud rate
//!
//! The line level is sampled by a timer paced DMA (hal_stm32/tmc2209_uart.cpp), so the
//! CPU does not have to watch the line with interrupts disabled. This turns the samples
//! into bytes (8N1, LSB first). Header only: tested on the PC (test/tmcuart).
#pragma once
#include <stdint.h>

#define TMC_RX_OVERSAMPLE 8 //!< samples per bit

//! @param raw   the samples (GPIO input data register values)
//! @param n     number of samples
//! @param mask  the bit of the line in a sample
//! @param out   bytes decoded
//! @param max_bytes size of out
//! @return number of bytes decoded: stops at the first framing error or the end of the data
static inline uint8_t tmc_rx_decode(const uint16_t *raw, uint16_t n, uint16_t mask, uint8_t *out, uint8_t max_bytes)
{
	const uint16_t OS = TMC_RX_OVERSAMPLE;
	auto lvl = [&](int32_t i) -> uint8_t { return (i >= 0 && i < (int32_t)n && (raw[i] & mask)) ? 1 : 0; };
	// majority of the sample at the centre of a bit and its two neighbours
	auto bit = [&](int32_t c) -> uint8_t { return (lvl(c - 1) + lvl(c) + lvl(c + 1)) >= 2; };

	int32_t i = 0;
	while (i < (int32_t)n && !lvl(i)) i++;  // the line is held low until the driver answers: wait for idle high
	while (i < (int32_t)n && lvl(i)) i++;   // start bit of the first byte: first low sample
	uint8_t k = 0;
	while (k < max_bytes && i < (int32_t)n) {
		const int32_t start = i;
		if (start + (int32_t)(OS * 10) > (int32_t)n) break; // the frame is not complete in the data
		// the start bit must still be low at its centre, the stop bit high at its centre
		if (bit(start + OS / 2)) break;
		if (!bit(start + OS * 9 + OS / 2)) break;
		uint8_t v = 0;
		for (uint8_t b = 0; b < 8; b++)
			if (bit(start + OS * (b + 1) + OS / 2)) v |= 1 << b;
		out[k++] = v;
		// the next start bit: a falling edge from the middle of the stop bit on (the driver
		// sends its bytes back to back); a gap of up to 20 bit times is allowed. The edge
		// found again re-synchronizes to the clock of the driver for every byte.
		int32_t j = start + OS * 9 + OS / 2;
		const int32_t last = start + OS * 10 + OS * 20;
		i = n;
		for (; j < last && j < (int32_t)n; j++)
			if (!lvl(j) && lvl(j - 1)) { i = j; break; }
	}
	return k;
}
