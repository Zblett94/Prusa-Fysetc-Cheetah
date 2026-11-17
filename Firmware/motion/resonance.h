//! @file
//! @brief Resonance measurement of the input shaper calibration (accelerometer)
//!
//! The axis is shaken at a series of frequencies (shaper_cal.cpp); for each one the
//! accelerometer samples are reduced to the amplitude of the response at that frequency
//! (Goertzel filter with a Hann window, gravity removed by a high-pass), divided by the
//! drive acceleration. analyze() finds the resonance in that curve: its frequency and its
//! damping ratio (half power bandwidth). Header only: tested on the PC (test/resonance).
#pragma once
#include <stdint.h>
#include <math.h>

namespace resonance {

constexpr int MAX_POINTS = 96;

//! slow high-pass for the three axes: takes the constant gravity out of the samples
struct HighPass {
    float lp[3];
    bool primed;
    float alpha; //!< 0.03 at 400 Hz: corner frequency 2 Hz
    void reset(float a = 0.03f) { primed = false; alpha = a; lp[0] = lp[1] = lp[2] = 0.f; }
    void process(const float in[3], float out[3]) {
        for (int i = 0; i < 3; ++i) {
            if (!primed) lp[i] = in[i];
            lp[i] += alpha * (in[i] - lp[i]);
            out[i] = in[i] - lp[i];
        }
        primed = true;
    }
};

//! Goertzel filter at one frequency for the three accelerometer axes, Hann windowed
struct Goertzel {
    float cosw, sinw, coeff;
    float s1[3], s2[3];
    float wsum;
    uint32_t n, n_total;
    //! @param f frequency (Hz), fs sample rate (Hz), samples number of samples of the window
    void start(float f, float fs, uint32_t samples) {
        const float w = 2.f * (float)M_PI * f / fs;
        cosw = cosf(w); sinw = sinf(w); coeff = 2.f * cosw;
        for (int i = 0; i < 3; ++i) s1[i] = s2[i] = 0.f;
        wsum = 0.f; n = 0; n_total = samples ? samples : 1;
    }
    void add(const float x[3]) {
        if (n >= n_total) return;
        const float win = 0.5f - 0.5f * cosf(2.f * (float)M_PI * ((float)n + 0.5f) / (float)n_total);
        wsum += win;
        for (int i = 0; i < 3; ++i) {
            const float s0 = x[i] * win + coeff * s1[i] - s2[i];
            s2[i] = s1[i]; s1[i] = s0;
        }
        ++n;
    }
    bool full() const { return n >= n_total; }
    //! amplitude of the sinusoid at the frequency, the three axes combined (sample units)
    float amplitude() const {
        if (wsum <= 0.f) return 0.f;
        float sum = 0.f;
        for (int i = 0; i < 3; ++i) {
            const float p = s1[i] * s1[i] + s2[i] * s2[i] - coeff * s1[i] * s2[i];
            sum += p > 0.f ? p : 0.f;
        }
        return 2.f * sqrtf(sum) / wsum; // sqrt(sum of the squared amplitudes of the axes)
    }
};

enum class Reason : uint8_t { ok, too_few_points, at_edge, weak_peak };

struct Result {
    bool ok;
    Reason reason;
    float freq;     //!< Hz
    float damping;  //!< damping ratio
    float peak;     //!< response at the peak (smoothed)
    float floor;    //!< lower quartile of the response
    float second;   //!< frequency of a second peak of at least 60 % of the first, 0 if none
};

//! @param freq frequencies (equally spaced, increasing), resp response / drive acceleration
inline Result analyze(const float *freq, const float *resp, int n) {
    Result r = {false, Reason::too_few_points, 0.f, 0.1f, 0.f, 0.f, 0.f};
    if (n < 7) return r;
    float rs[MAX_POINTS];
    if (n > MAX_POINTS) n = MAX_POINTS;
    rs[0] = resp[0]; rs[n - 1] = resp[n - 1];
    for (int i = 1; i < n - 1; ++i) rs[i] = 0.25f * resp[i - 1] + 0.5f * resp[i] + 0.25f * resp[i + 1];
    int imax = 0;
    for (int i = 1; i < n; ++i) if (rs[i] > rs[imax]) imax = i;
    // median (insertion sort of a copy)
    float srt[MAX_POINTS];
    for (int i = 0; i < n; ++i) srt[i] = rs[i];
    for (int i = 1; i < n; ++i) { const float v = srt[i]; int j = i - 1; while (j >= 0 && srt[j] > v) { srt[j + 1] = srt[j]; --j; } srt[j + 1] = v; }
    r.floor = srt[n / 4]; // lower quartile: the broad skirts of a resonance raise the median
    r.peak = rs[imax];
    if (imax == 0 || imax == n - 1) { r.reason = Reason::at_edge; r.freq = freq[imax]; return r; }
    if (!(r.peak >= 2.0f * r.floor) || r.peak <= 0.f) { r.reason = Reason::weak_peak; r.freq = freq[imax]; return r; }
    // parabolic interpolation of the peak
    const float a = rs[imax - 1], b = rs[imax], c = rs[imax + 1];
    const float den = a - 2.f * b + c;
    const float delta = (den < -1e-9f) ? 0.5f * (a - c) / den : 0.f;
    const float step = freq[imax + 1] - freq[imax];
    r.freq = freq[imax] + delta * step;
    // half power points (peak / sqrt 2), linear interpolation of the crossings
    const float thr = r.peak * 0.70710678f;
    float fl = -1.f, fr = -1.f;
    for (int j = imax; j > 0; --j) if (rs[j - 1] < thr) { fl = freq[j - 1] + (thr - rs[j - 1]) / (rs[j] - rs[j - 1]) * (freq[j] - freq[j - 1]); break; }
    for (int j = imax; j < n - 1; ++j) if (rs[j + 1] < thr) { fr = freq[j] + (rs[j] - thr) / (rs[j] - rs[j + 1]) * (freq[j + 1] - freq[j]); break; }
    float zeta = 0.1f;
    // the narrower side: a second resonance or the skirt of the response on one side would
    // widen the full width (the half width is damping * frequency)
    if (fl > 0.f && fr > 0.f) zeta = fminf(r.freq - fl, fr - r.freq) / r.freq;
    else if (fl > 0.f) zeta = (r.freq - fl) / r.freq;
    else if (fr > 0.f) zeta = (fr - r.freq) / r.freq;
    if (zeta < 0.04f) zeta = 0.04f;
    if (zeta > 0.3f) zeta = 0.3f;
    r.damping = zeta;
    // a second peak: local maximum, at least 5 Hz away, 60 % of the first
    for (int i = 1; i < n - 1; ++i) {
        if (rs[i] >= rs[i - 1] && rs[i] > rs[i + 1] && rs[i] >= 0.6f * r.peak && fabsf(freq[i] - r.freq) > 5.f
            && (r.second == 0.f || rs[i] > 0.f)) { r.second = freq[i]; break; }
    }
    r.ok = true; r.reason = Reason::ok;
    return r;
}

} // namespace resonance
