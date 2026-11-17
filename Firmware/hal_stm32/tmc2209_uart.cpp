//! @file
//! @brief TMC2209 single wire UART access
//!
//! All four drivers share one line, they are told apart by their slave address
//! (MS1/MS2 straps).
//!
//! SKR Mini E3 V3 (TMC_UART_TX_PIN/RX_PIN): USART4 on PC10/PC11. The board ties the TX
//! pin to the line through a resistor and the RX pin directly, so every transmitted byte
//! is also received (echo). The USART is polled: the accesses are short (a write
//! datagram takes 0.7 ms, a read 1.1 ms at 115200 Bd) and must also work from the power
//! panic interrupt handler, where an interrupt driven buffer could not drain.
//!
//! FYSETC Cheetah V3 (TMC_UART_PIN): the line is PB3, which has no UART function. The
//! bytes are sent bit-banged at 230400 Bd with the bit times taken from the cycle counter
//! (DWT); interrupts are disabled for each transmitted byte (43 us). The drivers detect
//! the baud rate from the sync byte of every datagram.
//! The reply is captured by DMA (TMC_UART_DMA): TIM4 paces DMA1 stream 6, which copies the
//! GPIOB input register to RAM 8 times per bit; the samples are decoded afterwards
//! (tmc2209_rx_decode.h, tested on the PC). The CPU is free and the interrupts enabled
//! meanwhile. If a decoded reply is not valid but a bit-banged retry works, the DMA path
//! is suspect: after 3 such cases in a row it is switched off (bit-banged reads, with the
//! interrupts disabled for the reply ~0.4 ms, as before).

#include <Arduino.h>
#include "hal.h"
#include "tmc2209_uart.h"
#include "../Configuration_var.h"
#include "../pins.h"

#define TMC_SYNC       0x05
#include "tmc2209_rx_decode.h"

static bool uart_ready;

static uint8_t tmc_crc(const uint8_t *data, uint8_t len)
{
	uint8_t crc = 0;
	for (uint8_t i = 0; i < len; i++) {
		uint8_t b = data[i];
		for (uint8_t j = 0; j < 8; j++) {
			if ((crc >> 7) ^ (b & 0x01))
				crc = (crc << 1) ^ 0x07;
			else
				crc = (crc << 1);
			b >>= 1;
		}
	}
	return crc;
}


#if defined(TMC_UART_TX_PIN)
//------------------------------------------------------------------------------
// USART (SKR Mini E3 V3)
//------------------------------------------------------------------------------
#define TMC_UART       USART4
#define TMC_UART_BAUD  115200UL
#define TMC_BYTE_TIMEOUT_US 2000

static void pin_af(uint32_t pin, uint32_t af, bool pullup)
{
	GPIO_TypeDef *port = _FIO_PORT(pin);
	const uint32_t b = fastio::pin_bit(pin);
	port->AFR[b >> 3] = (port->AFR[b >> 3] & ~(0xFUL << ((b & 7) * 4))) | (af << ((b & 7) * 4));
	port->PUPDR = (port->PUPDR & ~(3UL << (b * 2))) | ((pullup ? 1UL : 0UL) << (b * 2));
	port->OSPEEDR = (port->OSPEEDR & ~(3UL << (b * 2))) | (1UL << (b * 2));
	port->MODER = (port->MODER & ~(3UL << (b * 2))) | (2UL << (b * 2));
}

void tmc2209_uart_init()
{
	if (uart_ready) return;
	__HAL_RCC_USART4_CLK_ENABLE();
	TMC_UART->CR1 = 0;
	TMC_UART->BRR = (F_CPU + TMC_UART_BAUD / 2) / TMC_UART_BAUD;
	TMC_UART->CR2 = 0;
	TMC_UART->CR3 = USART_CR3_OVRDIS; // never block the receiver on an overrun
	TMC_UART->CR1 = USART_CR1_TE | USART_CR1_RE | USART_CR1_UE;
	pin_af(TMC_UART_TX_PIN, 1, true); // AF1 = USART4
	pin_af(TMC_UART_RX_PIN, 1, true);
	uart_ready = true;
}

static void rx_flush()
{
	while (TMC_UART->ISR & USART_ISR_RXNE_RXFNE)
		(void)TMC_UART->RDR;
	TMC_UART->ICR = USART_ICR_ORECF | USART_ICR_FECF | USART_ICR_NECF;
}

static void tx_byte(uint8_t b)
{
	uint32_t t0 = micros();
	while (!(TMC_UART->ISR & USART_ISR_TXE_TXFNF))
		if (micros() - t0 > TMC_BYTE_TIMEOUT_US) return;
	TMC_UART->TDR = b;
}

static bool rx_byte(uint8_t *b)
{
	uint32_t t0 = micros();
	while (!(TMC_UART->ISR & USART_ISR_RXNE_RXFNE))
		if (micros() - t0 > TMC_BYTE_TIMEOUT_US) return false;
	*b = TMC_UART->RDR;
	return true;
}

//! Send a datagram and swallow its echo
static bool tx_datagram(const uint8_t *d, uint8_t len)
{
	rx_flush();
	for (uint8_t i = 0; i < len; i++)
		tx_byte(d[i]);
	uint8_t echo;
	for (uint8_t i = 0; i < len; i++)
		if (!rx_byte(&echo)) return false;
	return true;
}

bool tmc2209_uart_write(uint8_t slave, uint8_t reg, uint32_t val)
{
#ifdef EMULATION
	(void)slave; (void)reg; (void)val;
	return true; // no TMC2209 model in the emulator
#endif
	tmc2209_uart_init();
	uint8_t d[8] = {
		TMC_SYNC, slave, (uint8_t)(reg | 0x80),
		(uint8_t)(val >> 24), (uint8_t)(val >> 16), (uint8_t)(val >> 8), (uint8_t)val, 0
	};
	d[7] = tmc_crc(d, 7);
	hal_uvlo_lock();
	const bool ok = tx_datagram(d, 8);
	hal_uvlo_unlock();
	// the driver needs 4 bit times of bus idle before the next datagram
	delayMicroseconds(40);
	return ok;
}

bool tmc2209_uart_read(uint8_t slave, uint8_t reg, uint32_t *val)
{
#ifdef EMULATION
	(void)slave;
	*val = (reg == 0x6F) ? 0x80000000UL : 0; // DRV_STATUS: standstill
	return true;
#endif
	tmc2209_uart_init();
	uint8_t req[4] = { TMC_SYNC, slave, (uint8_t)(reg & 0x7F), 0 };
	req[3] = tmc_crc(req, 3);
	for (uint8_t attempt = 0; attempt < 3; attempt++) {
		uint8_t rsp[8];
		bool ok;
		hal_uvlo_lock();
		ok = tx_datagram(req, 4);
		for (uint8_t i = 0; ok && i < 8; i++)
			ok = rx_byte(&rsp[i]);
		hal_uvlo_unlock();
		if (ok && rsp[0] == TMC_SYNC && rsp[1] == 0xFF && rsp[2] == (reg & 0x7F) && rsp[7] == tmc_crc(rsp, 7)) {
			*val = ((uint32_t)rsp[3] << 24) | ((uint32_t)rsp[4] << 16) | ((uint32_t)rsp[5] << 8) | rsp[6];
			delayMicroseconds(40);
			return true;
		}
		delayMicroseconds(500);
	}
	*val = 0;
	return false;
}

bool tmc2209_uart_nonblocking() { return true; } // polled, the interrupts stay enabled
void tmc2209_uart_report() {}

#elif defined(TMC_UART_PIN)
//------------------------------------------------------------------------------
// bit-banged single wire UART (FYSETC Cheetah V3, PB3)
//------------------------------------------------------------------------------
#define TMC_UART_BAUD  230400UL

static inline uint32_t cycles() { return DWT->CYCCNT; }

//! cycle count of bit edge i after t0 (rounded, no accumulated error)
static inline uint32_t bit_time(uint32_t t0, uint32_t i) {
	return t0 + (uint32_t)(((uint64_t)i * F_CPU + TMC_UART_BAUD / 2) / TMC_UART_BAUD);
}

static inline void wait_until(uint32_t t) {
	while ((int32_t)(cycles() - t) < 0) {}
}

static void line_output() {
	WRITE(TMC_UART_PIN, 1);
	SET_OUTPUT(TMC_UART_PIN);
}

static void line_input() {
	SET_INPUT(TMC_UART_PIN);
	WRITE(TMC_UART_PIN, 1); // pull-up
}

void tmc2209_uart_init()
{
	if (uart_ready) return;
	CoreDebug->DEMCR |= CoreDebug_DEMCR_TRCENA_Msk;
	DWT->CTRL |= DWT_CTRL_CYCCNTENA_Msk;
	line_output();
	delayMicroseconds(100);
	uart_ready = true;
}

#ifdef TMC_UART_DMA
//------------------------------------------------------------------------------
// reply capture by DMA
//------------------------------------------------------------------------------
#define RX_TIM          TIM4
#define RX_DMA          DMA1
#define RX_STREAM       DMA1_Stream6                // TIM4_UP: DMA1 stream 6, channel 2
#define RX_SAMPLES      (TMC_RX_OVERSAMPLE * 104)   // 8 bit times answer delay + 80 bit reply + margin
static uint16_t rx_samples[RX_SAMPLES];
static bool dma_enabled = true;
static uint8_t dma_suspect;
static uint16_t dma_reads, dma_fallbacks;

//! everything except the start (rx_dma_start): can be done before the last request byte
static void rx_dma_prepare()
{
	__HAL_RCC_DMA1_CLK_ENABLE();
	__HAL_RCC_TIM4_CLK_ENABLE();
	RX_STREAM->CR &= ~DMA_SxCR_EN;
	while (RX_STREAM->CR & DMA_SxCR_EN) {}
	RX_DMA->HIFCR = DMA_HIFCR_CTCIF6 | DMA_HIFCR_CHTIF6 | DMA_HIFCR_CTEIF6 | DMA_HIFCR_CDMEIF6 | DMA_HIFCR_CFEIF6;
	RX_STREAM->PAR = (uint32_t)&_FIO_PORT(TMC_UART_PIN)->IDR;
	RX_STREAM->M0AR = (uint32_t)rx_samples;
	RX_STREAM->NDTR = RX_SAMPLES;
	RX_STREAM->FCR = 0;                             // direct mode
	// channel 2, high priority, memory and peripheral halfword, memory increment,
	// peripheral to memory, no interrupts
	RX_STREAM->CR = DMA_SxCR_CHSEL_1 | DMA_SxCR_PL_1 | DMA_SxCR_MSIZE_0 | DMA_SxCR_PSIZE_0 | DMA_SxCR_MINC;
	RX_TIM->CR1 = 0;
	RX_TIM->DIER = 0;
	RX_TIM->PSC = 0;
	// APB1 timer clock = F_CPU / 2; rounded to the nearest (48.8 counts: -0.35 %, not -1.7 %)
	RX_TIM->ARR = ((F_CPU / 2) + (TMC_RX_OVERSAMPLE * TMC_UART_BAUD) / 2) / (TMC_RX_OVERSAMPLE * TMC_UART_BAUD) - 1;
	RX_TIM->CNT = 0;
	RX_TIM->EGR = TIM_EGR_UG;
	RX_TIM->SR = 0;
	RX_TIM->DIER = TIM_DIER_UDE;                    // DMA request at every update; the timer is not running yet
	memset(rx_samples, 0, sizeof(rx_samples));
}

//! the timer and the DMA start (a few cycles: called with the interrupts disabled, right
//! after the line was released)
static inline void rx_dma_start()
{
	RX_STREAM->CR |= DMA_SxCR_EN;
	RX_TIM->CR1 = TIM_CR1_CEN;
}

//! wait for the end of the capture (the interrupts are enabled); false on timeout
static bool rx_dma_wait()
{
	const uint32_t deadline = cycles() + (F_CPU / 1000) * 2; // 2 ms: the capture takes 0.45 ms
	while (!(RX_DMA->HISR & DMA_HISR_TCIF6))
		if ((int32_t)(cycles() - deadline) >= 0) return false;
	return true;
}

static void rx_dma_stop()
{
	RX_TIM->CR1 = 0;
	RX_TIM->DIER = 0;
	RX_STREAM->CR &= ~DMA_SxCR_EN;
	while (RX_STREAM->CR & DMA_SxCR_EN) {}
	RX_DMA->HIFCR = DMA_HIFCR_CTCIF6 | DMA_HIFCR_CHTIF6 | DMA_HIFCR_CTEIF6 | DMA_HIFCR_CDMEIF6 | DMA_HIFCR_CFEIF6;
}
#endif //TMC_UART_DMA

//! one byte, interrupts disabled for its 10 bit times
static void tx_byte(uint8_t b, bool release_after, bool start_capture = false)
{
	const uint32_t primask = __get_PRIMASK();
	__disable_irq();
	const uint32_t t0 = cycles() + 16;
	uint16_t frame = ((uint16_t)b << 1) | 0x200; // start bit 0, data LSB first, stop bit 1
	for (uint8_t i = 0; i < 10; i++, frame >>= 1) {
		wait_until(bit_time(t0, i));
		WRITE(TMC_UART_PIN, frame & 1);
	}
	wait_until(bit_time(t0, 10));
	if (release_after) line_input();
#ifdef TMC_UART_DMA
	if (start_capture) rx_dma_start(); // within a few cycles of the line release
#else
	(void)start_capture;
#endif
	__set_PRIMASK(primask);
}

static void tx_datagram(const uint8_t *d, uint8_t len, bool release_after)
{
	line_output();
	// idle (high) for a few bit times before the sync byte
	const uint32_t t0 = cycles();
	wait_until(bit_time(t0, 4));
	for (uint8_t i = 0; i < len; i++)
		tx_byte(d[i], release_after && i == len - 1);
}

//! wait until the line has the given level; false on timeout
static bool wait_level(bool level, uint32_t deadline)
{
	while (READ(TMC_UART_PIN) != level)
		if ((int32_t)(cycles() - deadline) >= 0) return false;
	return true;
}


//! receive a reply of n bytes (interrupts must be disabled)
static bool rx_reply(uint8_t *d, uint8_t n)
{
	const uint32_t bit = F_CPU / TMC_UART_BAUD;
	// the driver answers after SENDDELAY (8 bit times); the board pull-downs may hold
	// the line low until the driver drives it: wait for idle high first
	if (!wait_level(1, cycles() + 64 * bit)) return false;
	for (uint8_t k = 0; k < n; k++) {
		if (!wait_level(0, cycles() + (k ? 4 : 40) * bit)) return false; // start bit
		const uint32_t t0 = cycles();
		uint8_t b = 0;
		for (uint8_t i = 0; i < 8; i++) {
			wait_until(t0 + (uint32_t)(((uint64_t)(2 * i + 3) * F_CPU) / (2 * TMC_UART_BAUD))); // bit centre
			if (READ(TMC_UART_PIN)) b |= 1 << i;
		}
		wait_until(t0 + (uint32_t)(((uint64_t)19 * F_CPU) / (2 * TMC_UART_BAUD))); // stop bit centre
		if (!READ(TMC_UART_PIN)) return false; // framing error
		d[k] = b;
	}
	return true;
}

bool tmc2209_uart_write(uint8_t slave, uint8_t reg, uint32_t val)
{
#ifdef EMULATION
	(void)slave; (void)reg; (void)val;
	return true; // no TMC2209 model in the emulator
#endif
	tmc2209_uart_init();
	uint8_t d[8] = {
		TMC_SYNC, slave, (uint8_t)(reg | 0x80),
		(uint8_t)(val >> 24), (uint8_t)(val >> 16), (uint8_t)(val >> 8), (uint8_t)val, 0
	};
	d[7] = tmc_crc(d, 7);
	hal_uvlo_lock();
	tx_datagram(d, 8, false);
	hal_uvlo_unlock();
	delayMicroseconds(40); // >= 4 bit times of bus idle before the next datagram
	return true;
}

bool tmc2209_uart_read(uint8_t slave, uint8_t reg, uint32_t *val)
{
#ifdef EMULATION
	(void)slave;
	*val = (reg == 0x6F) ? 0x80000000UL : 0; // DRV_STATUS: standstill
	return true;
#endif
	tmc2209_uart_init();
	uint8_t req[4] = { TMC_SYNC, slave, (uint8_t)(reg & 0x7F), 0 };
	req[3] = tmc_crc(req, 3);
#ifdef TMC_UART_DMA
	bool use_dma = dma_enabled;
	bool dma_failed = false;
#endif
	for (uint8_t attempt = 0; attempt < 3; attempt++) {
		uint8_t rsp[8];
		bool ok;
#ifdef TMC_UART_DMA
		const bool via_dma = use_dma;
		if (via_dma) {
			hal_uvlo_lock();
			tx_datagram(req, 3, false);   // interrupts are disabled for each byte only
			rx_dma_prepare();
			tx_byte(req[3], true, true);  // the last byte releases the line and starts the capture
			const bool done = rx_dma_wait();
			rx_dma_stop();
			line_output();
			hal_uvlo_unlock();
			ok = done && tmc_rx_decode(rx_samples, RX_SAMPLES, 1 << fastio::pin_bit(TMC_UART_PIN), rsp, 8) == 8;
		} else
#endif
		{
			hal_uvlo_lock();
			const uint32_t primask = __get_PRIMASK();
			tx_datagram(req, 3, false);
			__disable_irq(); // from the last request byte to the end of the reply
			tx_byte(req[3], true);
			ok = rx_reply(rsp, 8);
			line_output();
			__set_PRIMASK(primask);
			hal_uvlo_unlock();
		}
		if (ok && rsp[0] == TMC_SYNC && rsp[1] == 0xFF && rsp[2] == (reg & 0x7F) && rsp[7] == tmc_crc(rsp, 7)) {
			*val = ((uint32_t)rsp[3] << 24) | ((uint32_t)rsp[4] << 16) | ((uint32_t)rsp[5] << 8) | rsp[6];
#ifdef TMC_UART_DMA
			if (via_dma) {
				dma_reads++;
				dma_suspect = 0;
			} else if (dma_failed && dma_enabled) {
				// the bit-banged retry worked where the DMA capture did not: suspect
				dma_fallbacks++;
				if (++dma_suspect >= 3) {
					dma_enabled = false;
					printf_P(PSTR("TMC UART: DMA capture failed 3 times, bit-banged reads from now on\n"));
				}
			}
#endif
			delayMicroseconds(40);
			return true;
		}
#ifdef TMC_UART_DMA
		if (via_dma) { use_dma = false; dma_failed = true; } // the next attempts bit-banged
#endif
		delayMicroseconds(500);
	}
	*val = 0;
	return false;
}

#ifdef TMC_UART_DMA
bool tmc2209_uart_nonblocking() { return dma_enabled && dma_reads > 0; }
void tmc2209_uart_report()
{
	printf_P(PSTR("TMC UART: DMA capture %s, %u DMA reads, %u fallbacks\n"), dma_enabled ? "on" : "OFF", dma_reads, dma_fallbacks);
}
#else
bool tmc2209_uart_nonblocking() { return false; }
void tmc2209_uart_report() {}
#endif

#endif // TMC_UART_PIN
