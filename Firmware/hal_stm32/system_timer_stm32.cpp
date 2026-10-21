//! @file
//! @brief STM32 replacement of timer02.c and tone04.c
//!
//! On the AVR, timer2 keeps the time (millis), timer0 drives the heatbed and
//! timer4 generates tones and the hotend fan PWM. Here the time comes from
//! SysTick, the heatbed from TIM1 (heatbed_pwm.cpp), tones from the Arduino
//! tone() on TIM4 and the hotend fan PWM from the soft PWM tick.

#include <Arduino.h>
#include "../system_timer.h"
#include "../Configuration_var.h"
#include "../pins.h"
#include "../fastio.h"
#include "hal.h"

void heatbed_pwm_init();

extern "C" {

void timer0_init(void)
{
	heatbed_pwm_init();
}

void timer2_init(void)
{
	// the time base (SysTick) is started by the Arduino core
}

unsigned long millis2(void) { return millis(); }
unsigned long micros2(void) { return micros(); }
void delay2(unsigned long ms) { delay(ms); }

void timer4_init(void)
{
	SET_OUTPUT(BEEPER);
	WRITE(BEEPER, LOW);
#ifdef EXTRUDER_0_AUTO_FAN_PIN
	SET_OUTPUT(EXTRUDER_0_AUTO_FAN_PIN);
#endif
}

void timer4_set_fan0(uint8_t duty)
{
	hal_hotend_fan_set(duty);
}

void tone4(uint8_t, uint16_t frequency)
{
	tone(BEEPER, frequency);
}

void noTone4(uint8_t)
{
	noTone(BEEPER);
	WRITE(BEEPER, 0);
}

void hal_delay_us(uint32_t us)
{
	delayMicroseconds(us);
}

} // extern "C"
