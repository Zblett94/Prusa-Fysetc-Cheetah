//! @file
//! @brief Input shaper calibration with an ADXL345 accelerometer
//!
//! The axis is shaken by short back and forth moves: with the acceleration a and the distance
//! d = a / (16 f^2) a move takes 1 / (2 f), so the acceleration is a square wave of the
//! frequency f. For each frequency of the sweep (15..95 Hz, 1 Hz steps, 0.45 s each) the
//! accelerometer samples are reduced to the amplitude of the response at f (resonance.h) and
//! divided by the drive. The peak of this curve is the resonance: the MZV shaper is offered at
//! that frequency with the damping measured from the width of the peak.
//! The sensor has to be on the moving part: the nozzle carriage for X, the heatbed for Y.
#include "shaper_cal.h"

#ifdef SHAPER_CAL
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
#include "motion/resonance.h"
#include "hal_stm32/accel_adxl345.h"

namespace {

#ifdef EMULATION
constexpr float F_START = 15.f, F_STEP = 5.f, F_END = 95.f; // coarse: the emulator is slow
#else
constexpr float F_START = 15.f, F_STEP = 1.f, F_END = 95.f;
#endif
constexpr float A_TARGET = 3000.f;   // mm/s^2 drive, raised at high frequencies (at least 4 steps per move)
constexpr float SETTLE_S = 0.15f;    // response is not measured while the oscillation builds up
constexpr float WINDOW_S = 0.30f;    // measurement window

float freqs[resonance::MAX_POINTS];
float resp[resonance::MAX_POINTS];

struct Saved {
    float acceleration, travel_acceleration, max_jerk;
    uint32_t max_accel, min_segment_time_us;
    input_shaper::AxisConfig shaper;
};

//! take the samples waiting in the FIFO: feed them to the filters (from the sample number
//! first_used on), or throw them away (g == nullptr); returns the new sample number
uint32_t drain(resonance::HighPass *hp, resonance::Goertzel *g, uint32_t first_used, uint32_t index)
{
    uint8_t n = accel_fifo_entries();
    while (n--) {
        int16_t raw[3];
        accel_read_sample(raw);
        if (g) {
            const float x[3] = { (float)raw[0], (float)raw[1], (float)raw[2] };
            float y[3];
            hp->process(x, y);
            if (index >= first_used) g->add(y);
        }
        ++index;
    }
    return index;
}

void keep_alive()
{
    manage_heater();
    manage_inactivity(true);
}

//! one frequency: returns false if the sensor stopped sending or lost samples
bool dwell(uint8_t axis, float f, float fs, float &response)
{
    const float spm = cs.axis_steps_per_mm[axis];
    float steps = floorf(A_TARGET / (16.f * f * f) * spm + 0.5f);
    if (steps < 4.f) steps = 4.f; // at least 4 steps: the distance is quantized to steps
    const float d = steps / spm;
    const float a = 16.f * f * f * d; // exact for the quantized distance
    cs.acceleration = cs.travel_acceleration = a;
    cs.max_acceleration_mm_per_s2_normal[axis] = (uint32_t)(a + 100.f);
    reset_acceleration_rates();
#ifdef EMULATION
    accel_emu_set_drive(f, a * (4.f / (float)M_PI) / ACCEL_LSB_MM_S2);
#endif
    const uint32_t settle = (uint32_t)(SETTLE_S * fs), window = (uint32_t)(WINDOW_S * fs);
    resonance::HighPass hp; hp.reset();
    resonance::Goertzel g; g.start(f, fs, window);
    (void)drain(nullptr, nullptr, 0, 0);
    (void)accel_overrun();
    uint32_t count = 0;
    float dir = 1.f;
    const uint32_t t_limit = millis() + 3000;
    while (count < settle + window) {
        count = drain(&hp, &g, settle, count);
        if (moves_planned() < 3) { // a few blocks ahead only: they run at this frequency
            set_destination_to_current();
            destination[axis] = current_position[axis] + dir * d;
            plan_buffer_line_destinationXYZE(200.f);
            current_position[axis] = destination[axis];
            dir = -dir;
        }
        keep_alive();
        if (millis() > t_limit) return false;
    }
    const uint32_t t_end = millis() + 1500;
    while (blocks_queued()) { // let the planner run empty
        (void)drain(nullptr, nullptr, 0, 0);
        keep_alive();
        if (millis() > t_end) return false;
    }
    if (accel_overrun()) return false;
    response = g.amplitude() / (a * (4.f / (float)M_PI) / ACCEL_LSB_MM_S2); // fundamental of the square wave: 4 / pi
    return true;
}

//! sample rate of the sensor: its clock is not exact (measured with the MCU clock over 2 s)
float measure_sample_rate()
{
    (void)drain(nullptr, nullptr, 0, 0);
    uint32_t count = 0;
    const uint32_t t0 = micros();
    uint32_t t1 = t0;
    while ((t1 = micros()) - t0 < 2000000UL) {
        uint8_t n = accel_fifo_entries();
        while (n--) { int16_t raw[3]; accel_read_sample(raw); ++count; }
        keep_alive();
    }
    return (float)count * 1e6f / (float)(t1 - t0);
}

void show(const char *line0, const char *line1 = nullptr)
{
    lcd_clear();
    lcd_puts_at_P(0, 0, line0);
    if (line1) lcd_puts_at_P(0, 1, line1);
}

void calibrate(uint8_t axis)
{
    const char name = axis == X_AXIS ? 'X' : 'Y';
    LcdUpdateDisabler lcdUpdateDisabler;
    if (printer_active()) return;
    if (tmc2130_mode != TMC2130_MODE_NORMAL) {
        lcd_show_fullscreen_message_and_wait_P(PSTR("Switch to Normal mode (Settings > Mode) for the calibration."));
        return;
    }
#ifndef EMULATION // (the emulator is not homed and has an SD card model)
    if (!(axis_known_position[X_AXIS] && axis_known_position[Y_AXIS] && axis_known_position[Z_AXIS])) {
        lcd_show_fullscreen_message_and_wait_P(PSTR("Home the printer first: Calibration > Auto home."));
        return;
    }
    if (IS_SD_INSERTED) {
        lcd_show_fullscreen_message_and_wait_P(PSTR("Remove the SD card from the LCD: the sensor uses its lines."));
        if (IS_SD_INSERTED) return;
    }
#endif
    if (!accel_init()) {
        lcd_show_fullscreen_message_and_wait_P(PSTR("ADXL345 not found. Check the wiring (docs/ACCELEROMETER.md)."));
        return;
    }
    lcd_show_fullscreen_message_and_wait_P(axis == X_AXIS
        ? PSTR("X: fix the sensor on the nozzle carriage. Click: the carriage shakes for 1 min.")
        : PSTR("Y: fix the sensor on the heatbed. Click: the bed shakes for 1 min."));

    // settings changed for the test, restored afterwards
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

    show(axis == X_AXIS ? PSTR("X shaper calibration") : PSTR("Y shaper calibration"), PSTR("Sensor rate..."));
    const float fs = measure_sample_rate();
    bool ok = fs > 300.f && fs < 500.f;
    printf_P(PSTR("Shaper cal %c: sensor rate %.1f Hz\n"), name, (double)fs);

    int n = 0;
    if (ok) {
        const float total = (F_END - F_START) / F_STEP + 1.f;
        for (float f = F_START; f <= F_END + 0.01f && n < resonance::MAX_POINTS; f += F_STEP, ++n) {
            lcd_set_cursor(0, 1);
            lcd_printf_P(PSTR("%3d Hz   %3d%%         "), (int)f, (int)(100.f * n / total));
            lcd_puts_at_P(0, 3, PSTR("Click to cancel"));
            if (lcd_clicked()) { ok = false; break; }
            float r = 0.f;
            if (!dwell(axis, f, fs, r) && !dwell(axis, f, fs, r)) { ok = false; break; } // one retry
            freqs[n] = f;
            resp[n] = r;
            printf_P(PSTR("RES %c %.0f %.4f\n"), name, (double)f, (double)r);
        }
    }
    // back to the settings of the printer, the carriage to its centre
    cs.acceleration = sv.acceleration; cs.travel_acceleration = sv.travel_acceleration;
    cs.max_jerk[axis] = sv.max_jerk; cs.max_acceleration_mm_per_s2_normal[axis] = sv.max_accel;
    cs.min_segment_time_us = sv.min_segment_time_us;
    reset_acceleration_rates();
    st_synchronize();
    current_position[axis] = centre;
    plan_buffer_line_curposXYZE(100.f);
    st_synchronize();
    motion_set_shaper(axis, sv.shaper);
    accel_stop();
    KEEPALIVE_STATE(NOT_BUSY);

    if (!ok) {
        lcd_show_fullscreen_message_and_wait_P(PSTR("Calibration stopped: no usable data from the sensor. Settings unchanged."));
        return;
    }
    const resonance::Result r = resonance::analyze(freqs, resp, n);
    printf_P(PSTR("Shaper cal %c: %s f=%.1f damping=%.3f peak=%.2f floor=%.2f second=%.1f\n"), name, r.ok ? "ok" : "failed",
             (double)r.freq, (double)r.damping, (double)r.peak, (double)r.floor, (double)r.second);
    if (!r.ok) {
        lcd_show_fullscreen_message_and_wait_P(r.reason == resonance::Reason::at_edge
            ? PSTR("Resonance at the end of the range (15-95 Hz): not usable. Check the mount.")
            : PSTR("No clear resonance found. Check the sensor mount (rigid!) and try again."));
        return;
    }
    lcd_clear();
    lcd_printf_P(PSTR("%c resonance %5.1f Hz\nDamping %5.2f\nMZV shaper at this\n"), name, (double)r.freq, (double)r.damping);
    lcd_encoder = 0;
    if (lcd_show_multiscreen_message_yes_no_and_wait_P(nullptr, false, LCD_LEFT_BUTTON_CHOICE) != LCD_LEFT_BUTTON_CHOICE) return;
    input_shaper::AxisConfig c = sv.shaper;
    c.enabled = true;
    c.type = input_shaper::Type::mzv;
    c.frequency = r.freq;
    c.damping_ratio = r.damping;
    motion_set_shaper(axis, c);
    motion_store_shaper_config();
    motion_report_shaper();
    lcd_show_fullscreen_message_and_wait_P(PSTR("Saved. Move the sensor for the other axis, or remove it."));
}

} // namespace

void shaper_cal_x() { calibrate(X_AXIS); }
void shaper_cal_y() { calibrate(Y_AXIS); }

#endif //SHAPER_CAL
