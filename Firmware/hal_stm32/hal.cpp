//! @file
//! @brief STM32G0B1 / STM32F446 hardware abstraction (timers, EXTI, watchdog, reset)

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
#if defined(STM32G0xx)
	__HAL_RCC_GPIOF_CLK_ENABLE();
#else
	__HAL_RCC_GPIOH_CLK_ENABLE();
#endif
}
} // namespace fastio

//------------------------------------------------------------------------------
// reset cause
//------------------------------------------------------------------------------

static uint8_t reset_cause;

uint8_t hal_reset_cause(void) { return reset_cause; }

static void read_reset_cause() {
	const uint32_t csr = RCC->CSR;
#if defined(STM32G0xx)
	const uint32_t power = RCC_CSR_PWRRSTF, other = RCC_CSR_OBLRSTF;
#else
	const uint32_t power = RCC_CSR_PORRSTF | RCC_CSR_BORRSTF, other = RCC_CSR_LPWRRSTF;
#endif
	reset_cause = 0;
	if (csr & (RCC_CSR_IWDGRSTF | RCC_CSR_WWDGRSTF)) reset_cause |= HAL_RST_WDRF;
	if (csr & power) reset_cause |= HAL_RST_PORF | HAL_RST_BORF;
	if (csr & RCC_CSR_SFTRSTF) reset_cause |= HAL_RST_WDRF; // the AVR firmware uses the watchdog for soft resets
	if ((csr & RCC_CSR_PINRSTF) && !(csr & (power | RCC_CSR_SFTRSTF | RCC_CSR_IWDGRSTF | RCC_CSR_WWDGRSTF | other)))
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
#ifdef EMULATION
	else {
		void hal_emulation_eeprom_preset();
		hal_emulation_eeprom_preset();
	}
#endif
}

void hal_reset(void) {
	NVIC_SystemReset();
}

#if defined(STM32G0xx)
#define BKP_REGS TAMP
static void backup_access() {
	__HAL_RCC_PWR_CLK_ENABLE();
	__HAL_RCC_RTCAPB_CLK_ENABLE();
	PWR->CR1 |= PWR_CR1_DBP;
}
#else
#define BKP_REGS RTC
static void backup_access() {
	__HAL_RCC_PWR_CLK_ENABLE();
	PWR->CR |= PWR_CR_DBP;
	// the backup registers are part of the RTC: it needs a clock source (LSI)
	if (!(RCC->BDCR & RCC_BDCR_RTCEN)) {
		RCC->CSR |= RCC_CSR_LSION;
		for (uint32_t i = 0; i < 100000 && !(RCC->CSR & RCC_CSR_LSIRDY); i++) {}
		RCC->BDCR = (RCC->BDCR & ~RCC_BDCR_RTCSEL) | RCC_BDCR_RTCSEL_1 | RCC_BDCR_RTCEN;
	}
}
#endif

void hal_kill_message_store(const char *msg) {
	backup_access();
	BKP_REGS->BKP0R = (uint32_t)(uintptr_t)msg;
	BKP_REGS->BKP1R = ~(uint32_t)(uintptr_t)msg;
}

const char *hal_kill_message_load(uint16_t low16, const char *fallback) {
	backup_access();
	const uint32_t p = BKP_REGS->BKP0R;
	if (BKP_REGS->BKP1R != ~p || (uint16_t)p != low16 || p < FLASH_BASE || p >= FLASH_BASE + HAL_FLASH_SIZE)
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

hal_isr_t hal_motion_tick;

static void softpwm_timer_cb() {
	softpwm_isr();
	__enable_irq();
	if (hal_motion_tick) hal_motion_tick();
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
	step_timer->setPrescaleFactor(STEP_TIMER_CLK / STEP_TIMER_FREQ);
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

//------------------------------------------------------------------------------
// vector table in RAM (for handlers that bypass the Arduino core dispatch)
//------------------------------------------------------------------------------

#if defined(STM32G0xx)
static constexpr uint32_t VECTOR_COUNT = 16 + 32; // STM32G0B1: 32 external interrupts
static uint32_t ram_vectors[VECTOR_COUNT] __attribute__((aligned(256)));
#else
static constexpr uint32_t VECTOR_COUNT = 16 + 97; // STM32F446: 97 external interrupts
static uint32_t ram_vectors[VECTOR_COUNT] __attribute__((aligned(512)));
#endif
static bool ram_vectors_active;

void hal_set_irq_handler(IRQn_Type irq, hal_isr_t handler) {
	if (!ram_vectors_active) {
		memcpy(ram_vectors, (const void *)SCB->VTOR, sizeof(ram_vectors));
		__disable_irq();
		SCB->VTOR = (uint32_t)ram_vectors;
		__DSB();
		__enable_irq();
		ram_vectors_active = true;
	}
	ram_vectors[16 + irq] = (uint32_t)handler;
}

//------------------------------------------------------------------------------
// motion engine step timer (STEP_TIMER, register level)
//------------------------------------------------------------------------------

void hal_motion_timer_init(hal_isr_t isr) {
	STEP_TIMER_CLK_ENABLE();
	STEP_TIMER->CR1 = 0;                 // stopped, ARR not buffered (AVR CTC behaviour)
	STEP_TIMER->CR1 |= TIM_CR1_URS;      // only overflows set the update flag
	STEP_TIMER->PSC = STEP_TIMER_CLK / STEP_TIMER_FREQ - 1;
	STEP_TIMER->ARR = 0xFFFF;
	STEP_TIMER->EGR = TIM_EGR_UG;        // load the prescaler
	STEP_TIMER->SR = 0;
	STEP_TIMER->DIER = 0;
	hal_set_irq_handler(STEP_TIMER_IRQn, isr);
	NVIC_SetPriority(STEP_TIMER_IRQn, HAL_PRIO_STEPPER);
	NVIC_ClearPendingIRQ(STEP_TIMER_IRQn);
	NVIC_EnableIRQ(STEP_TIMER_IRQn);
}

void hal_motion_timer_start(uint32_t period) {
	STEP_TIMER->CR1 &= ~TIM_CR1_CEN;
	STEP_TIMER->CNT = 0;
	STEP_TIMER->ARR = period - 1;
	STEP_TIMER->SR = 0;
	STEP_TIMER->DIER = TIM_DIER_UIE;
	STEP_TIMER->CR1 |= TIM_CR1_CEN;
}

void hal_motion_timer_stop(void) {
	STEP_TIMER->CR1 &= ~TIM_CR1_CEN;
	STEP_TIMER->DIER = 0;
	STEP_TIMER->SR = 0;
	NVIC_ClearPendingIRQ(STEP_TIMER_IRQn);
}

void hal_motion_timer_irq_enable(bool enable) {
	if (enable) STEP_TIMER->DIER |= TIM_DIER_UIE;
	else STEP_TIMER->DIER &= ~TIM_DIER_UIE;
}

#if defined(STM32G0xx)
//------------------------------------------------------------------------------
// ST7920 display transfer timer (TIM7, register level)
//------------------------------------------------------------------------------

static hal_isr_t lcd_isr;

static void lcd_timer_irq(void) {
	TIM7->SR = 0;
	lcd_isr();
}

void hal_lcd_timer_init(hal_isr_t isr) {
	lcd_isr = isr;
	__HAL_RCC_TIM7_CLK_ENABLE();
	TIM7->CR1 = 0;
	TIM7->PSC = F_CPU / 1000000UL - 1; // 1 MHz
	TIM7->ARR = 40 - 1;                // one byte every 40 us (a byte takes 10..16 us)
	TIM7->EGR = TIM_EGR_UG;
	TIM7->SR = 0;
	TIM7->DIER = TIM_DIER_UIE;
	hal_set_irq_handler(TIM7_LPTIM2_IRQn, lcd_timer_irq);
	NVIC_SetPriority(TIM7_LPTIM2_IRQn, HAL_PRIO_LCD);
	NVIC_ClearPendingIRQ(TIM7_LPTIM2_IRQn);
	NVIC_EnableIRQ(TIM7_LPTIM2_IRQn);
}

void hal_lcd_timer_kick(void) {
	if (!(TIM7->CR1 & TIM_CR1_CEN)) {
		TIM7->CNT = 0;
		TIM7->CR1 |= TIM_CR1_CEN;
	}
}

void hal_lcd_timer_stop(void) {
	TIM7->CR1 &= ~TIM_CR1_CEN;
}
#endif // STM32G0xx

void hal_softpwm_timer_init(hal_isr_t isr) {
	softpwm_isr = isr;
	if (!softpwm_timer) softpwm_timer = new HardwareTimer(SOFTPWM_TIMER);
	softpwm_timer->pause();
	softpwm_timer->setMode(1, TIMER_DISABLED);
	// 16MHz / 64 / 256 = 976.5625 Hz on the AVR -> 1 MHz / 1024 here
	softpwm_timer->setPrescaleFactor(SOFTPWM_TIMER_CLK / 1000000UL);
	softpwm_timer->setOverflow(1024, TICK_FORMAT);
	softpwm_timer->attachInterrupt(softpwm_timer_cb);
	softpwm_timer->setInterruptPriority(HAL_PRIO_SOFTPWM, 0);
	softpwm_timer->resume();
}

void hal_softpwm_isr_enable(bool enable) {
	if (enable) SOFTPWM_TIMER->DIER |= TIM_DIER_UIE;
	else SOFTPWM_TIMER->DIER &= ~TIM_DIER_UIE;
}

void hal_tempmgr_timer_init(hal_isr_t isr) {
	tempmgr_isr = isr;
	if (!tempmgr_timer) tempmgr_timer = new HardwareTimer(TEMPMGR_TIMER);
	tempmgr_timer->pause();
	tempmgr_timer->setMode(1, TIMER_DISABLED);
	// TEMP_MGR_INTV 0.27s: 10 kHz / 2700
	tempmgr_timer->setPrescaleFactor(TEMPMGR_TIMER_CLK / 10000UL);
	tempmgr_timer->setOverflow(2700, TICK_FORMAT);
	tempmgr_timer->attachInterrupt(tempmgr_timer_cb);
	tempmgr_timer->setInterruptPriority(HAL_PRIO_TEMPMGR, 0);
	tempmgr_timer->resume();
}

void hal_tempmgr_isr_enable(bool enable) {
	if (enable) TEMPMGR_TIMER->DIER |= TIM_DIER_UIE;
	else TEMPMGR_TIMER->DIER &= ~TIM_DIER_UIE;
}

//------------------------------------------------------------------------------
// external interrupts
//------------------------------------------------------------------------------

// register names of the two EXTI generations
#if defined(STM32G0xx)
static IRQn_Type exti_irqn(uint32_t line) {
	if (line <= 1) return EXTI0_1_IRQn;
	if (line <= 3) return EXTI2_3_IRQn;
	return EXTI4_15_IRQn;
}
#define EXTI_IMR   EXTI->IMR1
#define EXTI_RTSR  EXTI->RTSR1
#define EXTI_FTSR  EXTI->FTSR1
#define EXTI_SWIER EXTI->SWIER1
#define EXTI_CLEAR_PENDING(m) do { EXTI->RPR1 = (m); EXTI->FPR1 = (m); } while (0)
#else
static IRQn_Type exti_irqn(uint32_t line) {
	static const IRQn_Type low[5] = { EXTI0_IRQn, EXTI1_IRQn, EXTI2_IRQn, EXTI3_IRQn, EXTI4_IRQn };
	if (line <= 4) return low[line];
	if (line <= 9) return EXTI9_5_IRQn;
	return EXTI15_10_IRQn;
}
#define EXTI_IMR   EXTI->IMR
#define EXTI_RTSR  EXTI->RTSR
#define EXTI_FTSR  EXTI->FTSR
#define EXTI_SWIER EXTI->SWIER
#define EXTI_CLEAR_PENDING(m) (EXTI->PR = (m))
#endif

void hal_exti_attach(uint32_t pin, uint32_t mode, hal_isr_t isr) {
#ifdef EMULATION
	// the EXTI model of the Renode emulator does not match the STM32G0 registers
	// (the F4 one is not used either: same behaviour for both test builds)
	(void)pin; (void)mode; (void)isr;
	return;
#endif
	pin &= ~FASTIO_INVERTED;
	attachInterrupt(digitalPinToInterrupt(pin), isr, mode);
	HAL_NVIC_SetPriority(exti_irqn(fastio::pin_bit(pin)), HAL_PRIO_EXTI, 0);
}

void hal_exti_enable(uint32_t pin, bool enable) {
	const uint32_t m = fastio::mask(pin);
	if (enable) {
		EXTI_CLEAR_PENDING(m); // drop edges detected while masked
		EXTI_IMR |= m;
	} else {
		EXTI_IMR &= ~m;
	}
}

bool hal_exti_enabled(uint32_t pin) {
	return EXTI_IMR & fastio::mask(pin);
}

void hal_exti_set_edge(uint32_t pin, bool rising, bool falling) {
	const uint32_t m = fastio::mask(pin);
	if (rising) EXTI_RTSR |= m; else EXTI_RTSR &= ~m;
	if (falling) EXTI_FTSR |= m; else EXTI_FTSR &= ~m;
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
		EXTI_IMR &= ~fastio::mask(UVLO_PIN);
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
		EXTI_IMR |= fastio::mask(UVLO_PIN);
		// do not lose a power failure which happened while the line was masked
		if (!READ(UVLO_PIN))
			EXTI_SWIER = fastio::mask(UVLO_PIN);
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

#ifdef EMULATION
//! Renode test build: tachometer of the hotend fan, ~5000 RPM at full speed
//! (2 pulses per revolution, sampled on the 976 Hz tick)
bool emulation_tach0() {
	static uint32_t phase;
	phase += hotend_fan_duty;
	return (phase / 740) & 1;
}
#endif

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
