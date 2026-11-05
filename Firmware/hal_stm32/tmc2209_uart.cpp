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
//! bytes are bit-banged at 230400 Bd with the bit times taken from the cycle counter
//! (DWT). Interrupts are disabled for each transmitted byte (43 us) and while a driver
//! replies (~0.4 ms), so the timing is exact; the step interrupt is delayed by that much
//! at worst. The drivers detect the baud rate from the sync byte of every datagram.

#include <Arduino.h>
#include "hal.h"
#include "tmc2209_uart.h"
#include "../Configuration_var.h"
#include "../pins.h"

#define TMC_SYNC       0x05

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

//! one byte, interrupts disabled for its 10 bit times
static void tx_byte(uint8_t b, bool release_after)
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
	for (uint8_t attempt = 0; attempt < 3; attempt++) {
		uint8_t rsp[8];
		hal_uvlo_lock();
		const uint32_t primask = __get_PRIMASK();
		tx_datagram(req, 3, false);
		__disable_irq(); // from the last request byte to the end of the reply
		tx_byte(req[3], true);
		const bool ok = rx_reply(rsp, 8);
		line_output();
		__set_PRIMASK(primask);
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

#endif // TMC_UART_PIN
