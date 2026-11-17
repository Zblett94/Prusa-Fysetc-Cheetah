// Host test of the TMC2209 reply decoder (Firmware/hal_stm32/tmc2209_rx_decode.h):
// synthetic replies with random start delay, clock error between the sampling and the
// driver, back to back bytes and a line held low before the answer.
#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <vector>
#include "../../Firmware/hal_stm32/tmc2209_rx_decode.h"

static int failures, checks;
#define CHECK(c, ...) do { checks++; if (!(c)) { failures++; printf("FAIL %s:%d: ", __FILE__, __LINE__); printf(__VA_ARGS__); printf("\n"); } } while (0)

static uint8_t crc(const uint8_t *d, int len) {
	uint8_t c = 0;
	for (int i = 0; i < len; i++) { uint8_t b = d[i]; for (int j = 0; j < 8; j++) { c = ((c >> 7) ^ (b & 1)) ? (c << 1) ^ 7 : (c << 1); b >>= 1; } }
	return c;
}

// the line level at the time t (in bit times of the driver): idle high, bytes back to back
// from t_start on, optionally held low before
static int level(const std::vector<uint8_t> &bytes, double t_start, double t, double low_until) {
	if (t < low_until) return 0;
	double u = t - t_start;
	if (u < 0) return 1;
	int bi = (int)(u / 10.0);
	if (bi >= (int)bytes.size()) return 1;
	double w = u - bi * 10.0;
	if (w < 1.0) return 0;                                  // start bit
	if (w >= 9.0) return 1;                                 // stop bit
	return (bytes[bi] >> (int)(w - 1.0)) & 1;
}

static void run(const std::vector<uint8_t> &bytes, double t_start, double clock_ratio, double low_until, unsigned n, const char *what) {
	std::vector<uint16_t> raw(n);
	for (unsigned i = 0; i < n; i++) {
		// sample i at i / OS bit times of the sampler = i / OS * clock_ratio of the driver's
		int l = level(bytes, t_start, i / (double)TMC_RX_OVERSAMPLE * clock_ratio, low_until);
		raw[i] = (l ? (1 << 3) : 0) | 0x4A50; // other port pins in the sample: arbitrary
	}
	uint8_t out[16] = {0};
	uint8_t got = tmc_rx_decode(raw.data(), n, 1 << 3, out, 16);
	bool ok = got == bytes.size() && memcmp(out, bytes.data(), bytes.size()) == 0;
	CHECK(ok, "%s: start %.2f ratio %.3f -> %u bytes (want %zu)", what, t_start, clock_ratio, got, bytes.size());
}

int main() {
	srand(7);
	// a read reply: sync, 0xFF, register, 4 data bytes, crc
	for (int trial = 0; trial < 3000; trial++) {
		std::vector<uint8_t> r = { 0x05, 0xFF, (uint8_t)(rand() & 0x7F) };
		for (int i = 0; i < 4; i++) r.push_back(rand() & 0xFF);
		r.push_back(crc(r.data(), 7));
		const double start = 6.0 + (rand() % 4000) / 1000.0;           // 6..10 bit times after the capture started
		const double ratio = 0.97 + (rand() % 601) / 10000.0;          // +-3 % clock error
		const double low = (rand() & 1) ? 3.0 : 0.0;                   // held low at first
		run(r, start, ratio, low, 832, "reply");
	}
	// bytes all zero / all one (the long runs have no edges)
	run({0,0,0,0,0,0,0,0}, 8.0, 1.0, 0, 832, "zeros");
	run({0xFF,0xFF,0xFF,0xFF,0xFF,0xFF,0xFF,0xFF}, 8.0, 1.0, 0, 832, "ones");
	// the right amount of bytes only
	{
		std::vector<uint16_t> raw(832, 1 << 3); // nothing at all: idle line
		uint8_t out[8];
		CHECK(tmc_rx_decode(raw.data(), 832, 1 << 3, out, 8) == 0, "idle line");
		std::vector<uint16_t> low(832, 0);
		CHECK(tmc_rx_decode(low.data(), 832, 1 << 3, out, 8) == 0, "line held low");
	}
	// a truncated capture does not give a partial byte
	{
		std::vector<uint8_t> r = { 0x05, 0xFF, 0x06, 0, 0, 0, 0, 0 };
		r[7] = crc(r.data(), 7);
		std::vector<uint16_t> raw(400);
		for (unsigned i = 0; i < 400; i++) raw[i] = level(r, 8.0, i / (double)TMC_RX_OVERSAMPLE, 0) ? (1 << 3) : 0;
		uint8_t out[8];
		uint8_t got = tmc_rx_decode(raw.data(), 400, 1 << 3, out, 8);
		CHECK(got < 8 && got >= 3, "truncated: %u bytes", got);
		for (uint8_t i = 0; i < got; i++) CHECK(out[i] == r[i], "truncated byte %u", i);
	}
	printf("%d checks, %d failures\n", checks, failures);
	printf(failures ? "FAILED\n" : "OK\n");
	return failures != 0;
}
