//! @file
//! @brief Input shaper filters and configuration (STM32 motion engine)
//!
//! The filter formulas and the default configuration are those of the Prusa-Firmware-Buddy
//! (MK3.5 / MK4: MZV, X 50.7 Hz, Y 40.6 Hz, Y frequency lowered with the weight on the bed),
//! which are based on Klipper.
#pragma once
#include <stdint.h>

namespace input_shaper {

enum class Type : uint8_t {
    zv = 0,
    zvd,
    mzv,
    ei,
    ei_2hump,
    ei_3hump,
    null,
    cnt
};

constexpr uint8_t MAX_PULSES = 5;

struct AxisConfig {
    bool enabled;
    Type type;
    float frequency;           //!< Hz
    float damping_ratio;
    float vibration_reduction;
};

struct WeightAdjustConfig {
    bool enabled;
    float frequency_delta;     //!< Hz, frequency change at mass_limit (negative)
    float mass_limit;          //!< g
};

struct Config {
    AxisConfig axis[3];        //!< X, Y, Z
    WeightAdjustConfig weight_adjust_y;
};

//! Impulse train in the traditional definition: times t[i] >= 0 increasing, amplitudes a[i] > 0
struct Shaper {
    uint8_t num_pulses;
    float a[MAX_PULSES];
    float t[MAX_PULSES];       //!< s
};

//! Pulses ready for the motion engine: normalized amplitudes (sum 1), times centered around
//! their weighted average and converted to timer ticks. The shaped position is
//! x_s(t) = sum a[i] * x(t - t[i]).
struct Pulses {
    uint8_t num_pulses;
    float a[MAX_PULSES];
    int32_t t[MAX_PULSES];     //!< ticks
};

constexpr float frequency_safe_min = 10.f;
constexpr float frequency_safe_max = 100.f;
constexpr uint8_t low_freq_limit_hz = 35;
constexpr uint8_t high_freq_limit_hz = 70;

extern const AxisConfig axis_defaults[3];
extern const WeightAdjustConfig weight_adjust_y_default;

Shaper get(float damping_ratio, float shaper_freq, float vibration_reduction, Type type);
void make_pulses(Pulses &out, const AxisConfig &config, uint32_t tick_hz);
float clamp_frequency_to_safe_values(float frequency);
float weight_adjusted_frequency(float mass, const AxisConfig &axis, const WeightAdjustConfig &weight_adjust);
const char *to_string(Type type);

} // namespace input_shaper
