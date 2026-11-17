//! @file
//! @brief Frequency sweep of the X or Y axis for an external accelerometer
//! (Calibration > Shake X / Y, M975)
//!
//! The printer only shakes: a Raspberry Pi or ESP32 with an accelerometer (e.g. BTT S2DW)
//! records the motion and tools/shaker/shaker_analyze.py finds the resonance. The printer
//! reports the sweep on the serial port (docs/SHAKER.md). The shaper parameters are then
//! entered by hand: Calibration > Input shaper, PA (or M593).
#pragma once
#include "Configuration_var.h"

#ifdef MOTION_ENGINE
#define SHAKER
//! Menu items: prompt, sweep 15..95 Hz in 1 Hz steps (about 1 min), click cancels.
void shaker_x();
void shaker_y();
//! the same without prompts (M975 X|Y [S<start Hz>] [E<end Hz>] [I<step Hz>], from the Pi)
void shaker_sweep(bool x, float f0, float f1, float step);
#endif
