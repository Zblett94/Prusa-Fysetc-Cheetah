//! @file
//! @brief Motion engine of the STM32 port: firmware interface
//!
//! Input shaper and pressure advance settings (M593, M74, M572, M900) and their EEPROM
//! storage. The step interrupt and the planner glue are in stepper.cpp.
#pragma once
#include <stdint.h>
#include "input_shaper.h"

//! Load the settings from the EEPROM (or the defaults)
void motion_config_init();
//! Input shaper configuration in use
const input_shaper::Config &motion_shaper_config();
//! Change the shaper of an axis (X, Y, Z); waits until the motion stops.
void motion_set_shaper(uint8_t axis, const input_shaper::AxisConfig &config);
void motion_set_weight_adjust(const input_shaper::WeightAdjustConfig &config);
//! M74: weight on the bed (g), lowers the Y frequency (weight adjust)
void motion_set_bed_weight(float grams);
//! Store the input shaper configuration in the EEPROM (M593 W)
void motion_store_shaper_config();
//! Input shaper configuration as set (motion_shaper_config() has the Y frequency adjusted
//! to the bed weight of M74)
const input_shaper::Config &motion_shaper_setting();

//! Pressure advance (s) and its smoothing time (s); waits until the motion stops.
void motion_set_pressure_advance(float k, float smooth_time);
float motion_pressure_advance();
//! May the G-code (M572 S, M900 K of the slicer's filament profiles) change the pressure
//! advance? Default no: the value set on the LCD is used (Settings > Input shaper, PA).
bool motion_pa_from_gcode();
void motion_set_pa_from_gcode(bool on);
//! Back to the stored input shaper and pressure advance settings (start of a print: the
//! end G-code of the PrusaSlicer MK3.5 profiles switches both off with M593 F0 / M572 S0)
void motion_restore_defaults();
//! Store the pressure advance and its smoothing time as the power-on default (LCD menu).
//! M900 K / M572 S of a print still override it until the next start.
void motion_store_pressure_advance();
float motion_pa_smooth_time();

//! Report settings and statistics (M593 / M572 without parameters)
void motion_report_shaper();
void motion_report_pressure_advance();

// --- implemented in stepper.cpp ---
//! The engine settings changed, apply them before the next move
void motion_config_changed();
//! Wait until the motors stopped and the engine is idle
void motion_wait_idle();
//! Steps generated later than they should have been (the queue ran empty)
uint32_t motion_underruns();
//! CPU load (1/1000) of the producer and the step interrupt since the last call
void motion_load(uint16_t &producer_permille, uint16_t &isr_permille);
