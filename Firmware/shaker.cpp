//! @file
//! @brief Frequency sweep of the X or Y axis for an external accelerometer
//!
//! The axis is shaken by short back and forth moves: with the acceleration a and the distance
//! d = a / (16 f^2) a move takes 1 / (2 f), so the acceleration is a square wave of the
//! frequency f, whose fundamental has the amplitude 4 a / pi. Every frequency of the sweep is
//! driven for a fixed time. The serial port reports (the host timestamps the lines):
//!   SHAKE BEGIN <axis> settle_ms=<> window_ms=<> dwell_ms=<>
//!   SHAKE F <axis> <f Hz> <drive mm/s2>    (first move of the frequency queued)
//!   SHAKE END <axis> ok|cancelled|failed
#include "shaker.h"

#ifdef SHAKER
#include "Marlin.h"
#include "planner.h"
#include "stepper.h"
#include "temperature.h"
#include "ultralcd.h"
#include "lcd.h"
#include "cardreader.h"
#include "ConfigurationStore.h"
#include "tmc2130.h"
#include "motion/motion.h"

namespace {

constexpr float A_TARGET = 3000.f;   // mm/s^2 drive, raised at high frequencies (at least 4 steps per move)
constexpr uint16_t SETTLE_MS = 150;  // the host skips this part: the oscillation builds up
constexpr uint16_t WINDOW_MS = 300;  // and analyses this part
constexpr uint16_t DWELL_MS = 600;   // time moves are queued for each frequency

struct Saved {
    float acceleration, travel_acceleration, max_jerk;
    uint32_t max_accel, min_segment_time_us;
    input_shaper::AxisConfig shaper;
};

void keep_alive()
{
    manage_heater();
    manage_inactivity(true);
}

bool dwell(uint8_t axis, char name, float f, bool interactive)
{
    const float spm = cs.axis_steps_per_mm[axis];
    float steps = floorf(A_TARGET / (16.f * f * f) * spm + 0.5f);
    if (steps < 4.f) steps = 4.f; // at least 4 steps: the distance is quantized to steps
    const float d = steps / spm;
    const float a = 16.f * f * f * d; // exact for the quantized distance
    cs.acceleration = cs.travel_acceleration = a;
    cs.max_acceleration_mm_per_s2_normal[axis] = (uint32_t)(a + 100.f);
    reset_acceleration_rates();
    st_synchronize();
    printf_P(PSTR("SHAKE F %c %.1f %.0f\n"), name, (double)f, (double)a);
    float dir = 1.f;
    const uint32_t t_end = millis() + DWELL_MS;
    while ((int32_t)(millis() - t_end) < 0) {
        if (moves_planned() < 3) { // a few blocks ahead only: they run at this frequency
            set_destination_to_current();
            destination[axis] = current_position[axis] + dir * d;
            plan_buffer_line_destinationXYZE(200.f);
            current_position[axis] = destination[axis];
            dir = -dir;
        }
        keep_alive();
        if (interactive && lcd_clicked()) { st_synchronize(); return false; }
    }
    st_synchronize();
    return true;
}

//! returns false if cancelled or not possible
bool sweep(uint8_t axis, float f0, float f1, float step, bool interactive)
{
    const char name = axis == X_AXIS ? 'X' : 'Y';
    if (printer_active()) return false;
    if (tmc2130_mode != TMC2130_MODE_NORMAL) {
        puts_P(PSTR("SHAKE END ? failed: normal mode needed"));
        return false;
    }
    if (!(axis_known_position[X_AXIS] && axis_known_position[Y_AXIS] && axis_known_position[Z_AXIS])) {
        puts_P(PSTR("SHAKE END ? failed: home first"));
        return false;
    }
    if (!(f0 >= 10.f && f1 <= 150.f && f1 >= f0 && step >= 0.5f)) {
        puts_P(PSTR("SHAKE END ? failed: range 10..150 Hz, step >= 0.5"));
        return false;
    }
    Saved sv = { cs.acceleration, cs.travel_acceleration, cs.max_jerk[axis], cs.max_acceleration_mm_per_s2_normal[axis],
                 cs.min_segment_time_us, motion_shaper_setting().axis[axis] };
    input_shaper::AxisConfig off = sv.shaper;
    off.enabled = false;
    motion_set_shaper(axis, off);
    cs.max_jerk[axis] = 0.01f;     // every move starts and ends at rest
    cs.min_segment_time_us = 0;    // the short moves must not be slowed down
    KEEPALIVE_STATE(IN_HANDLER);

    raise_z_above(MIN_Z_FOR_LOAD);
    current_position[X_AXIS] = (X_MIN_POS + X_MAX_POS) / 2.f;
    current_position[Y_AXIS] = (Y_MIN_POS + Y_MAX_POS) / 2.f;
    plan_buffer_line_curposXYZE(100.f);
    st_synchronize();
    const float centre = current_position[axis];

    printf_P(PSTR("SHAKE BEGIN %c settle_ms=%u window_ms=%u dwell_ms=%u\n"), name, SETTLE_MS, WINDOW_MS, DWELL_MS);
    bool ok = true;
    const float total = (f1 - f0) / step + 1.f;
    float n = 0;
    for (float f = f0; f <= f1 + 0.01f; f += step, n += 1.f) {
        if (interactive) {
            lcd_set_cursor(0, 1);
            lcd_printf_P(PSTR("%3d Hz   %3d%%         "), (int)f, (int)(100.f * n / total));
            lcd_puts_at_P(0, 3, PSTR("Click to cancel"));
        }
        if (!dwell(axis, name, f, interactive)) { ok = false; break; }
    }
    printf_P(PSTR("SHAKE END %c %s\n"), name, ok ? "ok" : "cancelled");

    cs.acceleration = sv.acceleration; cs.travel_acceleration = sv.travel_acceleration;
    cs.max_jerk[axis] = sv.max_jerk; cs.max_acceleration_mm_per_s2_normal[axis] = sv.max_accel;
    cs.min_segment_time_us = sv.min_segment_time_us;
    reset_acceleration_rates();
    st_synchronize();
    current_position[axis] = centre;
    plan_buffer_line_curposXYZE(100.f);
    st_synchronize();
    motion_set_shaper(axis, sv.shaper);
    KEEPALIVE_STATE(NOT_BUSY);
    return ok;
}

void menu(uint8_t axis)
{
    LcdUpdateDisabler lcdUpdateDisabler;
    if (printer_active()) return;
    if (tmc2130_mode != TMC2130_MODE_NORMAL) {
        lcd_show_fullscreen_message_and_wait_P(PSTR("Switch to Normal mode (Settings > Mode) first."));
        return;
    }
    if (!(axis_known_position[X_AXIS] && axis_known_position[Y_AXIS] && axis_known_position[Z_AXIS])) {
        lcd_show_fullscreen_message_and_wait_P(PSTR("Home the printer first: Calibration > Auto home."));
        return;
    }
    lcd_show_fullscreen_message_and_wait_P(axis == X_AXIS
        ? PSTR("X: sensor on the nozzle carriage, start the recording on the Pi, then click: 1 min of shaking.")
        : PSTR("Y: sensor on the heatbed, start the recording on the Pi, then click: 1 min of shaking."));
    lcd_clear();
    lcd_puts_at_P(0, 0, axis == X_AXIS ? PSTR("X shaking") : PSTR("Y shaking"));
    const bool ok = sweep(axis, 15.f, 95.f, 1.f, true);
    lcd_show_fullscreen_message_and_wait_P(ok
        ? PSTR("Done. Stop the recording, analyse it, enter the result in Calibration > Input shaper.")
        : PSTR("Stopped."));
}

} // namespace

void shaker_x() { menu(X_AXIS); }
void shaker_y() { menu(Y_AXIS); }

void shaker_sweep(bool x, float f0, float f1, float step) { (void)sweep(x ? X_AXIS : Y_AXIS, f0, f1, step, false); }

#endif //SHAKER
