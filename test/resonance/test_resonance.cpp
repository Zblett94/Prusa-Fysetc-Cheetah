// Host test of the resonance analysis (Firmware/motion/resonance.h): a simulated accelerometer
// on a mass-spring system with noise, gravity and a sample clock that is a bit off.
#include <stdio.h>
#include <stdlib.h>
#include <random>
#include "../../Firmware/motion/resonance.h"

static int failures, checks;
#define CHECK(c, ...) do { checks++; if (!(c)) { failures++; printf("FAIL %s:%d: ", __FILE__, __LINE__); printf(__VA_ARGS__); printf("\n"); } } while (0)

// transmissibility of a base driven mass-spring-damper (acceleration out / acceleration in)
static double H(double f, double f0, double zeta) {
	const double r = f / f0;
	return sqrt(1 + pow(2 * zeta * r, 2)) / sqrt(pow(1 - r * r, 2) + pow(2 * zeta * r, 2));
}

struct Sim { double f0, zeta, f1, zeta1, g1; };

// run the sweep like shaper_cal.cpp does and return the curve
static int sweep(const Sim &s, double fs_true, double fs_est, float *freqs, float *resp, std::mt19937 &rng, double noise_lsb) {
	std::normal_distribution<double> nd(0.0, noise_lsb);
	int n = 0;
	for (double f = 15; f <= 95.01; f += 1.0, ++n) {
		const double spm = 100;
		const double a_target = 3000;
		double steps = floor(a_target / (16 * f * f) * spm + 0.5);
		if (steps < 4) steps = 4;
		const double a = 16 * f * f * steps / spm;                 // mm/s^2 drive (fundamental of the square wave: x 4/pi)
		const double a_lsb = a * (4 / M_PI) / 38.25;               // 3.9 mg per LSB
		const double h = H(f, s.f0, s.zeta) + s.g1 * H(f, s.f1, s.zeta1);
		const uint32_t settle = (uint32_t)(0.15 * fs_est), window = (uint32_t)(0.30 * fs_est);
		resonance::HighPass hp; hp.reset();
		resonance::Goertzel g; g.start((float)f, (float)fs_est, window);
		for (uint32_t k = 0; k < settle + window; k++) {
			const double t = k / fs_true;
			const double sig = a_lsb * h * sin(2 * M_PI * f * t + 0.7);
			float x[3] = { (float)(sig * 0.8 + nd(rng)), (float)(sig * 0.5 + nd(rng) + 13), (float)(256 + sig * 0.33 + nd(rng)) };
			float y[3];
			hp.process(x, y);
			if (k >= settle) g.add(y);
		}
		freqs[n] = (float)f;
		resp[n] = g.amplitude() / (float)a_lsb;
	}
	return n;
}

int main() {
	std::mt19937 rng(11);
	float fr[resonance::MAX_POINTS], rp[resonance::MAX_POINTS];
	// X like axis: 47 Hz, damping 0.1, a weak second resonance at 82 Hz
	for (int trial = 0; trial < 40; trial++) {
		Sim s = { 35.0 + (rng() % 300) / 10.0, 0.06 + (rng() % 100) / 1000.0, 82, 0.05, 0.25 };
		const double fs_true = 400 * (0.93 + (rng() % 140) / 1000.0); // the sensor clock is up to +-7 % off
		const double fs_est = fs_true * (1 + ((int)(rng() % 21) - 10) / 4000.0); // measured to +-0.25 %
		const int n = sweep(s, fs_true, fs_est, fr, rp, rng, 2.0);
		const resonance::Result r = resonance::analyze(fr, rp, n);
		CHECK(r.ok, "trial %d: not found (reason %d, f0 %.1f)", trial, (int)r.reason, s.f0);
		CHECK(fabs(r.freq - s.f0) < 1.0, "trial %d: f0 %.2f, found %.2f", trial, s.f0, r.freq);
		CHECK(r.damping > 0.7 * s.zeta && r.damping < 1.5 * s.zeta, "trial %d: zeta %.3f, found %.3f", trial, s.zeta, r.damping);
	}
	// no resonance in the range: a flat curve with noise only
	{
		Sim s = { 300, 0.1, 400, 0.1, 0 };
		const int n = sweep(s, 400, 400, fr, rp, rng, 2.0);
		const resonance::Result r = resonance::analyze(fr, rp, n);
		CHECK(!r.ok, "flat curve: found %.1f Hz", r.freq);
	}
	// the resonance above the range: the peak at the edge
	{
		Sim s = { 140, 0.1, 400, 0.1, 0 };
		const int n = sweep(s, 400, 400, fr, rp, rng, 2.0);
		const resonance::Result r = resonance::analyze(fr, rp, n);
		CHECK(!r.ok, "above the range: found %.1f Hz", r.freq);
	}
	printf("%d checks, %d failures\n", checks, failures);
	printf(failures ? "FAILED\n" : "OK\n");
	return failures != 0;
}
