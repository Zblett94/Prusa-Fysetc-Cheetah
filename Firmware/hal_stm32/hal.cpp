//! @file
//! @brief STM32G0B1 hardware abstraction (timers, EXTI, watchdog, reset)

#include <Arduino.h>
#include <HardwareTimer.h>
#include "hal.h"
#include "../Configuration.h"
#include "../pins.h"
#include "include/avr/wdt.h"
#include "include/avr/eeprom.h"

namespace fastio {
void init() {
	__HAL_RCC_GPIOA_CLK_ENABLE();
	__HAL_RCC_GPIOB_CLK_ENABLE();
	__HAL_RCC_GPIOC_CLK_ENABLE();
	__HAL_RCC_GPIOD_CLK_ENABLE();
	__HAL_RCC_GPIOF_CLK_ENABLE();
}
} // namespace fastio

//------------------------------------------------------------------------------
// reset cause
//------------------------------------------------------------------------------

static uint8_t reset_cause;

uint8_t hal_reset_cause(void) { return reset_cause; }

static void read_reset_cause() {
	const uint32_t csr = RCC->CSR;
	reset_cause = 0;
	if (csr & (RCC_CSR_IWDGRSTF | RCC_CSR_WWDGRSTF)) reset_cause |= HAL_RST_WDRF;
	if (csr & RCC_CSR_PWRRSTF) reset_cause |= HAL_RST_PORF | HAL_RST_BORF;
	if (csr & RCC_CSR_SFTRSTF) reset_cause |= HAL_RST_WDRF; // the AVR firmware uses the watchdog for soft resets
	if ((csr & RCC_CSR_PINRSTF) && !(csr & (RCC_CSR_PWRRSTF | RCC_CSR_SFTRSTF | RCC_CSR_IWDGRSTF | RCC_CSR_WWDGRSTF | RCC_CSR_OBLRSTF)))
		reset_cause |= HAL_RST_EXTRF;
	RCC->CSR |= RCC_CSR_RMVF;
}

void hal_init(void) {
	fastio::init();
	read_reset_cause();
}

extern "C" bool hal_eeprom_init(void);
extern "C" void hal_eeprom_erase(void);

//! Runs right after the STM32 core init() (constructor 101) and before the static
//! constructors and setup() of the firmware, which already read the EEPROM.
__attribute__((constructor(102))) static void hal_early_init(void) {
	hal_init();
	if (hal_eeprom_init()) {
		// The AT24C32 may hold the settings of another firmware (e.g. Marlin).
		// Start from the state of a virgin ATmega2560 (all 0xFF) in that case,
		// exactly like a new EINSy board.
		static const char magic[] = FW_PRUSA3D_MAGIC;
		bool prusa = true, empty = true;
		for (uint8_t i = 0; i < sizeof(magic); i++) {
			const uint8_t b = eeprom_read_byte((const uint8_t *)(uintptr_t)i);
			if (b != (uint8_t)magic[i]) prusa = false;
			if (b != 0xFF) empty = false;
		}
		if (!prusa && !empty)
			hal_eeprom_erase();
	}
}

void hal_reset(void) {
	NVIC_SystemReset();
}

static void backup_access() {
	__HAL_RCC_PWR_CLK_ENABLE();
	__HAL_RCC_RTCAPB_CLK_ENABLE();
	PWR->CR1 |= PWR_CR1_DBP;
}

void hal_kill_message_store(const char *msg) {
	backup_access();
	TAMP->BKP0R = (uint32_t)(uintptr_t)msg;
	TAMP->BKP1R = ~(uint32_t)(uintptr_t)msg;
}

const char *hal_kill_message_load(uint16_t low16, const char *fallback) {
	backup_access();
	const uint32_t p = TAMP->BKP0R;
	if (TAMP->BKP1R != ~p || (uint16_t)p != low16 || p < FLASH_BASE || p >= FLASH_BASE + 512UL * 1024UL)
		return fallback; // the backup domain was reset (power cycle)
	return (const char *)(uintptr_t)p;
}

//------------------------------------------------------------------------------
// timers
//------------------------------------------------------------------------------

static HardwareTimer *step_timer;
static HardwareTimer *softpwm_timer;
static HardwareTimer *tempmgr_timer;

static hal_isr_t step_isr;
static hal_isr_t softpwm_isr;
static hal_isr_t tempmgr_isr;

// On AVR an ISR returns with "reti", which always re-enables interrupts, even
// when the ISR body executed cli(). On Cortex-M PRIMASK is not restored on
// exception return, so the wrappers below re-enable interrupts explicitly
// (an IRQ can only be taken while PRIMASK is clear).

static void step_timer_cb() {
	step_isr();
	__enable_irq();
}

static void softpwm_timer_cb() {
	softpwm_isr();
	__enable_irq();
}

static void tempmgr_timer_cb() {
	tempmgr_isr();
	__enable_irq();
}

void hal_step_timer_init(hal_isr_t isr) {
	step_isr = isr;
	if (!step_timer) step_timer = new HardwareTimer(STEP_TIMER);
	step_timer->pause();
	step_timer->setMode(1, TIMER_DISABLED);
	step_timer->setPrescaleFactor(F_CPU / STEP_TIMER_FREQ);
	step_timer->setOverflow(0x4000, TICK_FORMAT);
	step_timer->setPreloadEnable(false); // OCR1A writes take effect immediately (AVR CTC behaviour)
	step_timer->attachInterrupt(step_timer_cb);
	step_timer->setInterruptPriority(HAL_PRIO_STEPPER, 0);
	step_timer->resume();
}

void hal_step_timer_isr_enable(bool enable) {
	if (enable) STEP_TIMER->DIER |= TIM_DIER_UIE;
	else STEP_TIMER->DIER &= ~TIM_DIER_UIE;
}

void hal_softpwm_timer_init(hal_isr_t isr) {
	softpwm_isr = isr;
	if (!softpwm_timer) softpwm_timer = new HardwareTimer(TIM16);
	softpwm_timer->pause();
	softpwm_timer->setMode(1, TIMER_DISABLED);
	// 16MHz / 64 / 256 = 976.5625 Hz on the AVR -> 1 MHz / 1024 here
	softpwm_timer->setPrescaleFactor(F_CPU / 1000000UL);
	softpwm_timer->setOverflow(1024, TICK_FORMAT);
	softpwm_timer->attachInterrupt(softpwm_timer_cb);
	softpwm_timer->setInterruptPriority(HAL_PRIO_SOFTPWM, 0);
	softpwm_timer->resume();
}

void hal_softpwm_isr_enable(bool enable) {
	if (enable) TIM16->DIER |= TIM_DIER_UIE;
	else TIM16->DIER &= ~TIM_DIER_UIE;
}

void hal_tempmgr_timer_init(hal_isr_t isr) {
	tempmgr_isr = isr;
	if (!tempmgr_timer) tempmgr_timer = new HardwareTimer(TIM15);
	tempmgr_timer->pause();
	tempmgr_timer->setMode(1, TIMER_DISABLED);
	// TEMP_MGR_INTV 0.27s: 10 kHz / 2700
	tempmgr_timer->setPrescaleFactor(F_CPU / 10000UL);
	tempmgr_timer->setOverflow(2700, TICK_FORMAT);
	tempmgr_timer->attachInterrupt(tempmgr_timer_cb);
	tempmgr_timer->setInterruptPriority(HAL_PRIO_TEMPMGR, 0);
	tempmgr_timer->resume();
}

void hal_tempmgr_isr_enable(bool enable) {
	if (enable) TIM15->DIER |= TIM_DIER_UIE;
	else TIM15->DIER &= ~TIM_DIER_UIE;
}

//------------------------------------------------------------------------------
// external interrupts
//------------------------------------------------------------------------------

static IRQn_Type exti_irqn(uint32_t line) {
	if (line <= 1) return EXTI0_1_IRQn;
	if (line <= 3) return EXTI2_3_IRQn;
	return EXTI4_15_IRQn;
}

void hal_exti_attach(uint32_t pin, uint32_t mode, hal_isr_t isr) {
	pin &= ~FASTIO_INVERTED;
	attachInterrupt(digitalPinToInterrupt(pin), isr, mode);
	HAL_NVIC_SetPriority(exti_irqn(fastio::pin_bit(pin)), HAL_PRIO_EXTI, 0);
}

void hal_exti_enable(uint32_t pin, bool enable) {
	const uint32_t m = fastio::mask(pin);
	if (enable) {
		EXTI->RPR1 = m; // drop edges detected while masked
		EXTI->FPR1 = m;
		EXTI->IMR1 |= m;
	} else {
		EXTI->IMR1 &= ~m;
	}
}

bool hal_exti_enabled(uint32_t pin) {
	return EXTI->IMR1 & fastio::mask(pin);
}

void hal_exti_set_edge(uint32_t pin, bool rising, bool falling) {
	const uint32_t m = fastio::mask(pin);
	if (rising) EXTI->RTSR1 |= m; else EXTI->RTSR1 &= ~m;
	if (falling) EXTI->FTSR1 |= m; else EXTI->FTSR1 &= ~m;
}

static uint8_t uvlo_lock_depth;
static bool uvlo_was_enabled;

void hal_uvlo_lock(void) {
#ifdef UVLO_PIN
	if (hal_in_isr()) return;
	const uint32_t primask = __get_PRIMASK();
	__disable_irq();
	if (uvlo_lock_depth++ == 0) {
		uvlo_was_enabled = hal_exti_enabled(UVLO_PIN);
		EXTI->IMR1 &= ~fastio::mask(UVLO_PIN);
	}
	__set_PRIMASK(primask);
#endif
}

void hal_uvlo_unlock(void) {
#ifdef UVLO_PIN
	if (hal_in_isr()) return;
	const uint32_t primask = __get_PRIMASK();
	__disable_irq();
	if (uvlo_lock_depth && --uvlo_lock_depth == 0 && uvlo_was_enabled) {
		EXTI->IMR1 |= fastio::mask(UVLO_PIN);
		// do not lose a power failure which happened while the line was masked
		if (!READ(UVLO_PIN))
			EXTI->SWIER1 = fastio::mask(UVLO_PIN);
	}
	__set_PRIMASK(primask);
#endif
}

//------------------------------------------------------------------------------
// hotend fan (software PWM, 4 bit like FAN_SOFT_PWM, on the 976 Hz tick)
//------------------------------------------------------------------------------

static volatile uint8_t hotend_fan_duty;

void hal_hotend_fan_set(uint8_t duty) {
	hotend_fan_duty = duty;
#ifdef EXTRUDER_0_AUTO_FAN_PIN
	if (duty == 0 || duty == 255)
		WRITE(EXTRUDER_0_AUTO_FAN_PIN, duty);
#endif
}

void hal_hotend_fan_tick(void) {
#ifdef EXTRUDER_0_AUTO_FAN_PIN
	static uint8_t cnt;
	const uint8_t duty = hotend_fan_duty;
	if (duty == 0 || duty == 255) return;
	cnt = (cnt + 1) & 0x0F;
	WRITE(EXTRUDER_0_AUTO_FAN_PIN, cnt < (duty >> 4));
#endif
}

//------------------------------------------------------------------------------
// watchdog (IWDG, LSI 32 kHz)
//------------------------------------------------------------------------------

static bool wdt_running;
static volatile bool wdt_auto_feed;

static void iwdg_config(uint32_t ms) {
	// prescaler 256 -> 125 Hz, 8 ms per tick, 4095 ticks max (32.7 s)
	uint32_t reload = ms / 8;
	if (reload < 2) reload = 2;
	if (reload > 0x0FFF) reload = 0x0FFF;
	IWDG->KR = 0xCCCC; // start (also the LSI)
	IWDG->KR = 0x5555; // unlock PR/RLR
	IWDG->PR = 6;      // /256
	IWDG->RLR = reload;
	uint32_t timeout = 100000;
	while (IWDG->SR && --timeout) {}
	IWDG->KR = 0xAAAA; // reload
	wdt_running = true;
}

extern "C" void wdt_enable(uint8_t wdto) {
	static const uint16_t ms[] = { 15, 30, 60, 120, 250, 500, 1000, 2000, 4000, 8000 };
	wdt_auto_feed = false;
	iwdg_config(ms[wdto < 10 ? wdto : 9]);
}

extern "C" void wdt_reset(void) {
	if (wdt_running) IWDG->KR = 0xAAAA;
}

extern "C" void wdt_disable(void) {
	// The IWDG can not be stopped once started: while "disabled" it is fed
	// from the SysTick interrupt instead.
	wdt_auto_feed = true;
	wdt_reset();
}

// called by the STM32 core from the SysTick (1 ms) interrupt
extern "C" void osSystickHandler(void) {
	if (wdt_auto_feed) wdt_reset();
}
