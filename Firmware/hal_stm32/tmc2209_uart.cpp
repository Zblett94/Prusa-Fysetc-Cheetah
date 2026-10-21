//! @file
//! @brief TMC2209 single wire UART access (USART4 on PC10/PC11 of the SKR Mini E3 V3)
//!
//! All four drivers share one line, they are told apart by their slave address
//! (MS1/MS2 straps). The board ties the TX pin to the line through a resistor
//! and the RX pin directly, so every transmitted byte is also received (echo).
//!
//! The USART is polled: the accesses are short (a write datagram takes 0.7 ms,
//! a read 1.1 ms at 115200 Bd) and must also work from the power panic
//! interrupt handler, where an interrupt driven buffer could not drain.

#include <Arduino.h>
#include "hal.h"
#include "tmc2209_uart.h"
#include "../Configuration_var.h"
#include "../pins.h"

#define TMC_UART       USART4
#define TMC_UART_BAUD  115200UL
#define TMC_SYNC       0x05
#define TMC_BYTE_TIMEOUT_US 2000

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
