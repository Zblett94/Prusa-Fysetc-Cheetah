//! @file
//! @brief Motion engine settings: input shaper (M593, M74), pressure advance (M572, M900)
#include "../Marlin.h"
#include "../eeprom.h"
#include "../planner.h"
#include "motion.h"
#include "motion_core.h"

#ifdef MOTION_ENGINE

namespace {

//! EEPROM image of the input shaper configuration
struct __attribute__((packed)) StoredAxis {
    uint8_t enabled;
    uint8_t type;
    float frequency;
    float damping_ratio;
    float vibration_reduction;
};
struct __attribute__((packed)) StoredShaper {
    uint8_t magic;
    StoredAxis axis[3];
    uint8_t wa_enabled;
    float wa_frequency_delta;
    float wa_mass_limit;
};
static_assert(sizeof(StoredShaper) <= EEPROM_INPUT_SHAPER_SIZE, "input shaper EEPROM area too small");
constexpr uint8_t STORED_MAGIC = 0x5A;

input_shaper::Config config;   // as configured (M593 / EEPROM)
input_shaper::Config active;   // in use: Y frequency adjusted to the bed weight (M74)
float bed_weight;              // g, 0 = not set
float pa_smooth_time = 0.04f;  // s (Buddy default)

bool valid_axis(const StoredAxis &a) {
    return a.enabled <= 1 && a.type < uint8_t(input_shaper::Type::cnt)
        && a.frequency >= input_shaper::frequency_safe_min && a.frequency <= input_shaper::frequency_safe_max
        && a.damping_ratio >= 0.f && a.damping_ratio < 1.f && a.vibration_reduction > 0.f;
}

void update_active() {
    active = config;
    const input_shaper::WeightAdjustConfig &wa = config.weight_adjust_y;
    if (bed_weight > 0.f && wa.enabled && config.axis[Y_AXIS].enabled) {
        active.axis[Y_AXIS].frequency = input_shaper::clamp_frequency_to_safe_values(
            input_shaper::weight_adjusted_frequency(bed_weight, config.axis[Y_AXIS], wa));
    }
    motion_config_changed();
}

} // namespace

void motion_config_init() {
    for (uint8_t i = 0; i < 3; ++i)
        config.axis[i] = input_shaper::axis_defaults[i];
    config.weight_adjust_y = input_shaper::weight_adjust_y_default;

    StoredShaper s;
    eeprom_read_block(&s, (const void *)EEPROM_INPUT_SHAPER, sizeof(s));
    if (s.magic == STORED_MAGIC) {
        for (uint8_t i = 0; i < 3; ++i) {
            if (!valid_axis(s.axis[i])) continue;
            config.axis[i].enabled = s.axis[i].enabled;
            config.axis[i].type = input_shaper::Type(s.axis[i].type);
            config.axis[i].frequency = s.axis[i].frequency;
            config.axis[i].damping_ratio = s.axis[i].damping_ratio;
            config.axis[i].vibration_reduction = s.axis[i].vibration_reduction;
        }
        if (s.wa_enabled <= 1 && s.wa_frequency_delta <= 0.f && s.wa_mass_limit >= 0.f) {
            config.weight_adjust_y.enabled = s.wa_enabled;
            config.weight_adjust_y.frequency_delta = s.wa_frequency_delta;
            config.weight_adjust_y.mass_limit = s.wa_mass_limit;
        }
    }
    bed_weight = 0.f;
    update_active();
}

void motion_store_shaper_config() {
    StoredShaper s;
    s.magic = STORED_MAGIC;
    for (uint8_t i = 0; i < 3; ++i) {
        const input_shaper::AxisConfig &a = config.axis[i];
        s.axis[i].enabled = a.enabled;
        s.axis[i].type = uint8_t(a.type);
        s.axis[i].frequency = a.frequency;
        s.axis[i].damping_ratio = a.damping_ratio;
        s.axis[i].vibration_reduction = a.vibration_reduction;
    }
    s.wa_enabled = config.weight_adjust_y.enabled;
    s.wa_frequency_delta = config.weight_adjust_y.frequency_delta;
    s.wa_mass_limit = config.weight_adjust_y.mass_limit;
    eeprom_update_block(&s, (void *)EEPROM_INPUT_SHAPER, sizeof(s));
}

const input_shaper::Config &motion_shaper_config() {
    return active;
}

void motion_set_shaper(uint8_t axis, const input_shaper::AxisConfig &c) {
    if (axis > Z_AXIS) return;
    motion_wait_idle();
    config.axis[axis] = c;
    if (c.enabled)
        config.axis[axis].frequency = input_shaper::clamp_frequency_to_safe_values(c.frequency);
    update_active();
}

void motion_set_weight_adjust(const input_shaper::WeightAdjustConfig &c) {
    motion_wait_idle();
    config.weight_adjust_y = c;
    update_active();
}

void motion_set_bed_weight(float grams) {
    motion_wait_idle();
    bed_weight = grams;
    update_active();
}

void motion_set_pressure_advance(float k, float smooth_time) {
    if (k == extruder_advance_K && smooth_time == pa_smooth_time) return;
    motion_wait_idle();
    extruder_advance_K = k;
    pa_smooth_time = smooth_time;
    motion_config_changed();
}

float motion_pressure_advance() { return extruder_advance_K; }
float motion_pa_smooth_time() { return pa_smooth_time; }

static void report_axis(char name, const input_shaper::AxisConfig &a) {
    SERIAL_ECHO_START;
    if (!a.enabled) {
        printf_P(PSTR("axis %c disabled\n"), name);
        return;
    }
    printf_P(PSTR("axis %c type=%s freq=%.2f damp=%.3f vr=%.1f\n"), name, input_shaper::to_string(a.type),
        (double)a.frequency, (double)a.damping_ratio, (double)a.vibration_reduction);
}

void motion_report_shaper() {
    report_axis('X', active.axis[X_AXIS]);
    report_axis('Y', active.axis[Y_AXIS]);
    report_axis('Z', active.axis[Z_AXIS]);
    SERIAL_ECHO_START;
    if (config.weight_adjust_y.enabled)
        printf_P(PSTR("weight_adjust y freq_delta=%.2f mass_limit=%.1f bed_weight=%.1f\n"),
            (double)config.weight_adjust_y.frequency_delta, (double)config.weight_adjust_y.mass_limit, (double)bed_weight);
    else
        puts_P(PSTR("weight adjust y disabled"));
    uint16_t load_producer, load_isr;
    motion_load(load_producer, load_isr);
    SERIAL_ECHO_START;
    printf_P(PSTR("motion: %lu blocks, %lu planner underruns, %lu step underruns, load %u.%u%% + %u.%u%%\n"),
        (unsigned long)motion::stats.blocks, (unsigned long)motion::stats.gaps, (unsigned long)motion_underruns(),
        load_producer / 10, load_producer % 10, load_isr / 10, load_isr % 10);
}

void motion_report_pressure_advance() {
    SERIAL_ECHO_START;
    if (extruder_advance_K <= 0.f)
        puts_P(PSTR("Pressure advance: disabled"));
    else
        printf_P(PSTR("Pressure advance: %.4f, smooth time: %.3f\n"), (double)extruder_advance_K, (double)pa_smooth_time);
}

#endif // MOTION_ENGINE
