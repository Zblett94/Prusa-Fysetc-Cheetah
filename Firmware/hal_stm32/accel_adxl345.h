//! @file
//! @brief ADXL345 accelerometer on SPI1, for the input shaper calibration (shaper_cal.cpp)
//!
//! Cheetah V3: the sensor takes the place of the SD card of the LCD: it is wired to the SD
//! card lines of EXP2 (SCK, MISO, MOSI and the SD chip select as its chip select, see
//! docs/ACCELEROMETER.md); the card must be removed during the calibration. SPI mode 3, 2.8 MHz.
#pragma once
#include <stdint.h>

#ifdef ACCEL_ADXL345
#define ACCEL_SAMPLE_RATE 400.f   //!< nominal sample rate (Hz); the real one is measured
#define ACCEL_LSB_MM_S2   38.25f  //!< full resolution mode: 3.9 mg per LSB in mm/s^2

//! Configure the sensor (400 Hz, +-16 g full resolution, FIFO stream mode) and start measuring.
//! False if no ADXL345 answers (wrong device id).
bool accel_init();
//! number of samples waiting in the FIFO (0..32)
uint8_t accel_fifo_entries();
//! take the oldest sample from the FIFO
void accel_read_sample(int16_t xyz[3]);
//! true if the FIFO overflowed since the last call (samples were lost)
bool accel_overrun();
//! standby, release the bus
void accel_stop();
#ifdef EMULATION
//! emulator build: the sensor is simulated, a resonance at 47 Hz; amplitude (LSB) and frequency
//! of the drive, set by the calibration for every frequency
void accel_emu_set_drive(float freq_hz, float amplitude_lsb);
#endif
#endif //ACCEL_ADXL345
