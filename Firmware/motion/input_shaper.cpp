//! @file
//! @brief Input shaper filters, ported from Prusa-Firmware-Buddy
//! (lib/Marlin/Marlin/src/feature/input_shaper), which is based on Klipper:
//! Copyright (C) Dmitry Butyugin <dmbutyugin@google.com>
//! Copyright (C) Kevin O'Connor <kevin@koconnor.net>
#include "input_shaper.h"
#include <math.h>

namespace input_shaper {

// Defaults of the MK3.5/MK4 (Buddy input_shaper_config.hpp)
const AxisConfig axis_defaults[3] = {
    { true, Type::mzv, 50.7f, 0.1f, 20.f },
    { true, Type::mzv, 40.6f, 0.1f, 20.f },
    { false, Type::null, 0.f, 0.f, 0.f },
};

const WeightAdjustConfig weight_adjust_y_default = { true, -20.f, 800.f };

static inline float sqr(float x) { return x * x; }

Shaper get(const float damping_ratio, const float shaper_freq, const float vibration_reduction, const Type type) {
    Shaper s = {};
    if (!(shaper_freq > 0.f) || !(damping_ratio < 1.f)) {
        s.num_pulses = 1;
        s.a[0] = 1.f;
        return s;
    }
    const float df = sqrtf(1.f - sqr(damping_ratio));
    const float t_d = 1.f / (shaper_freq * df);
    switch (type) {
    case Type::zv: {
        const float K = expf(-damping_ratio * float(M_PI) / df);
        s.num_pulses = 2;
        s.a[0] = 1.f; s.a[1] = K;
        s.t[0] = 0.f; s.t[1] = .5f * t_d;
        break;
    }
    case Type::zvd: {
        const float K = expf(-damping_ratio * float(M_PI) / df);
        s.num_pulses = 3;
        s.a[0] = 1.f; s.a[1] = 2.f * K; s.a[2] = sqr(K);
        s.t[0] = 0.f; s.t[1] = .5f * t_d; s.t[2] = t_d;
        break;
    }
    case Type::mzv: {
        const float K = expf(-.75f * damping_ratio * float(M_PI) / df);
        const float a1 = 1.f - 1.f / sqrtf(2.f);
        s.num_pulses = 3;
        s.a[0] = a1; s.a[1] = (sqrtf(2.f) - 1.f) * K; s.a[2] = a1 * K * K;
        s.t[0] = 0.f; s.t[1] = .375f * t_d; s.t[2] = .75f * t_d;
        break;
    }
    case Type::ei: {
        const float v_tol = 1.f / vibration_reduction;
        const float K = expf(-damping_ratio * float(M_PI) / df);
        const float a1 = .25f * (1.f + v_tol);
        s.num_pulses = 3;
        s.a[0] = a1; s.a[1] = .5f * (1.f - v_tol) * K; s.a[2] = a1 * K * K;
        s.t[0] = 0.f; s.t[1] = .5f * t_d; s.t[2] = t_d;
        break;
    }
    case Type::ei_2hump: {
        const float v_tol = 1.f / vibration_reduction;
        const float K = expf(-damping_ratio * float(M_PI) / df);
        const float V2 = sqr(v_tol);
        const float X = powf(V2 * (sqrtf(1.f - V2) + 1.f), 1.f / 3.f);
        const float a1 = (3.f * X * X + 2.f * X + 3.f * V2) / (16.f * X);
        const float a2 = (.5f - a1) * K;
        s.num_pulses = 4;
        s.a[0] = a1; s.a[1] = a2; s.a[2] = a2 * K; s.a[3] = a1 * K * K * K;
        s.t[0] = 0.f; s.t[1] = .5f * t_d; s.t[2] = t_d; s.t[3] = 1.5f * t_d;
        break;
    }
    case Type::ei_3hump: {
        const float v_tol = 1.f / vibration_reduction;
        const float K = expf(-damping_ratio * float(M_PI) / df);
        const float K2 = K * K;
        const float a1 = 0.0625f * (1.f + 3.f * v_tol + 2.f * sqrtf(2.f * (v_tol + 1.f) * v_tol));
        const float a2 = 0.25f * (1.f - v_tol) * K;
        s.num_pulses = 5;
        s.a[0] = a1; s.a[1] = a2; s.a[2] = (0.5f * (1.f + v_tol) - 2.f * a1) * K2; s.a[3] = a2 * K2; s.a[4] = a1 * K2 * K2;
        s.t[0] = 0.f; s.t[1] = .5f * t_d; s.t[2] = t_d; s.t[3] = 1.5f * t_d; s.t[4] = 2.f * t_d;
        break;
    }
    default:
        s.num_pulses = 1;
        s.a[0] = 1.f;
        break;
    }
    return s;
}

void make_pulses(Pulses &out, const AxisConfig &config, const uint32_t tick_hz) {
    const Shaper s = config.enabled
        ? get(config.damping_ratio, config.frequency, config.vibration_reduction, config.type)
        : get(0.f, 0.f, 0.f, Type::null);
    // Normalize the amplitudes (the shaped move reaches the original target) and center the
    // pulses around their weighted average time, so that shaped and unshaped axes stay in sync.
    // single precision: the F4 FPU has no double (the double library would cost 3 KiB of
    // flash); the pulse times are rounded to timer ticks anyway
    float sum_a = 0.f;
    for (uint8_t i = 0; i < s.num_pulses; ++i)
        sum_a += s.a[i];
    float avg_t = 0.f;
    for (uint8_t i = 0; i < s.num_pulses; ++i)
        avg_t += s.a[i] / sum_a * s.t[i];
    out.num_pulses = s.num_pulses;
    for (uint8_t i = 0; i < s.num_pulses; ++i) {
        out.a[i] = float(s.a[i] / sum_a);
        out.t[i] = int32_t(lroundf((float(s.t[i]) - avg_t) * tick_hz));
    }
}

float clamp_frequency_to_safe_values(float frequency) {
    if (frequency < frequency_safe_min) return frequency_safe_min;
    if (frequency > frequency_safe_max) return frequency_safe_max;
    return frequency;
}

// M74: the Y resonance frequency drops with the mass on the bed (Buddy M74.cpp)
float weight_adjusted_frequency(float mass, const AxisConfig &axis, const WeightAdjustConfig &weight_adjust) {
    const float frequency_limit = axis.frequency + weight_adjust.frequency_delta;
    if (mass > weight_adjust.mass_limit)
        return frequency_limit;
    if (weight_adjust.mass_limit == 0.f)
        return axis.frequency;
    const float t = mass / weight_adjust.mass_limit;
    return axis.frequency + t * (frequency_limit - axis.frequency);
}

const char *to_string(Type type) {
    static const char *const names[] = { "ZV", "ZVD", "MZV", "EI", "EI_2HUMP", "EI_3HUMP", "Null" };
    const uint8_t idx = uint8_t(type);
    return idx < uint8_t(Type::cnt) ? names[idx] : "Unknown";
}

} // namespace input_shaper
