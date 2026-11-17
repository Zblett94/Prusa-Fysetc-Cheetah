//! @file
//! @brief Input shaper calibration with an ADXL345 accelerometer (Calibration > Shaper cal. X / Y)
#pragma once
#include "Configuration_var.h"
#include "pins.h"

#if defined(ACCEL_ADXL345) && defined(MOTION_ENGINE)
#define SHAPER_CAL
//! Shake the X (sensor on the nozzle carriage) or the Y axis (sensor on the heatbed) through a
//! range of frequencies, find the resonance, offer the MZV shaper at that frequency. Blocking.
void shaper_cal_x();
void shaper_cal_y();
#endif
