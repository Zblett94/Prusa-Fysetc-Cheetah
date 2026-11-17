//! @file
//! @brief ADXL345 accelerometer on SPI1, see accel_adxl345.h
#include <Arduino.h>
#include "hal.h"
#include "../Configuration_var.h"
#include "../pins.h"
#include "accel_adxl345.h"

#ifdef ACCEL_ADXL345

#ifndef EMULATION
//------------------------------------------------------------------------------
// the sensor on the SD card lines
//------------------------------------------------------------------------------
#define ADXL_CS SDSS
#define ADXL_AF 5UL // SPI1 on PA5/PA6/PA7

static void pin_af(uint32_t pin)
{
	GPIO_TypeDef *port = _FIO_PORT(pin);
	const uint32_t b = fastio::pin_bit(pin);
	port->AFR[b >> 3] = (port->AFR[b >> 3] & ~(0xFUL << ((b & 7) * 4))) | (ADXL_AF << ((b & 7) * 4));
	port->OSPEEDR |= (3UL << (b * 2));
	port->MODER = (port->MODER & ~(3UL << (b * 2))) | (2UL << (b * 2));
}

//! SPI mode 3 (CPOL 1, CPHA 1), APB2 / 32 = 2.8 MHz (the ADXL345 allows 5 MHz)
static void spi_begin()
{
	__HAL_RCC_SPI1_CLK_ENABLE();
	pin_af(SD_SCK_PIN);
	pin_af(SD_MISO_PIN);
	pin_af(SD_MOSI_PIN);
	SET_OUTPUT(ADXL_CS);
	WRITE(ADXL_CS, 1);
	const uint32_t cr1 = SPI_CR1_MSTR | SPI_CR1_SSM | SPI_CR1_SSI | SPI_CR1_CPOL | SPI_CR1_CPHA | (4UL << SPI_CR1_BR_Pos);
	SPI1->CR1 = 0;
	SPI1->CR2 = 0; // 8 bit frames
	SPI1->CR1 = cr1;
	SPI1->CR1 = cr1 | SPI_CR1_SPE;
}

static uint8_t xfer(uint8_t b)
{
	while (!(SPI1->SR & SPI_SR_TXE)) {}
	*(volatile uint8_t *)&SPI1->DR = b;
	while (!(SPI1->SR & SPI_SR_RXNE)) {}
	return *(volatile uint8_t *)&SPI1->DR;
}

static uint8_t reg_read(uint8_t reg)
{
	WRITE(ADXL_CS, 0);
	xfer(0x80 | reg);
	const uint8_t v = xfer(0);
	WRITE(ADXL_CS, 1);
	return v;
}

static void reg_write(uint8_t reg, uint8_t val)
{
	WRITE(ADXL_CS, 0);
	xfer(reg);
	xfer(val);
	WRITE(ADXL_CS, 1);
}

#define REG_DEVID       0x00
#define REG_INT_SOURCE  0x30
#define REG_BW_RATE     0x2C
#define REG_POWER_CTL   0x2D
#define REG_DATA_FORMAT 0x31
#define REG_DATAX0      0x32
#define REG_FIFO_CTL    0x38
#define REG_FIFO_STATUS 0x39

bool accel_init()
{
	spi_begin();
	delay(2);
	if (reg_read(REG_DEVID) != 0xE5) return false;
	reg_write(REG_POWER_CTL, 0x00);   // standby while configuring
	reg_write(REG_BW_RATE, 0x0C);     // 400 Hz output data rate
	reg_write(REG_DATA_FORMAT, 0x0B); // full resolution, +-16 g
	reg_write(REG_FIFO_CTL, 0x00);    // bypass: clears the FIFO
	reg_write(REG_FIFO_CTL, 0x80);    // stream mode
	reg_write(REG_POWER_CTL, 0x08);   // measure
	delay(20);
	if (reg_read(REG_DATA_FORMAT) != 0x0B) return false; // the registers do not hold what was written
	(void)reg_read(REG_INT_SOURCE);   // clear the overrun flag
	while (accel_fifo_entries()) { int16_t d[3]; accel_read_sample(d); }
	return true;
}

uint8_t accel_fifo_entries()
{
	return reg_read(REG_FIFO_STATUS) & 0x3F;
}

void accel_read_sample(int16_t xyz[3])
{
	WRITE(ADXL_CS, 0);
	xfer(0xC0 | REG_DATAX0);          // read, multiple bytes
	uint8_t b[6];
	for (uint8_t i = 0; i < 6; i++) b[i] = xfer(0);
	WRITE(ADXL_CS, 1);
	for (uint8_t i = 0; i < 3; i++) xyz[i] = (int16_t)((uint16_t)b[2 * i] | ((uint16_t)b[2 * i + 1] << 8));
	delayMicroseconds(5);             // the FIFO needs 5 us to move the next sample to the output registers
}

bool accel_overrun()
{
	return reg_read(REG_INT_SOURCE) & 0x01;
}

void accel_stop()
{
	reg_write(REG_POWER_CTL, 0x00);
	WRITE(ADXL_CS, 1);
}

#else //EMULATION
//------------------------------------------------------------------------------
// simulated sensor: a resonance at 47 Hz (damping 0.1), a little noise, gravity on Z
//------------------------------------------------------------------------------
static float emu_freq = 50.f, emu_amp = 0.f;
static uint32_t emu_t0_us, emu_consumed;
static bool emu_running;

static float emu_gain(float f)
{
	const float f0 = 47.f, z = 0.1f, r = f / f0;
	const float a = 1.f - r * r, b = 2.f * z * r;
	return sqrtf(1.f + b * b) / sqrtf(a * a + b * b);
}

void accel_emu_set_drive(float freq_hz, float amplitude_lsb)
{
	emu_freq = freq_hz;
	emu_amp = amplitude_lsb * emu_gain(freq_hz);
}

bool accel_init() { emu_t0_us = micros(); emu_consumed = 0; emu_running = true; return true; }

uint8_t accel_fifo_entries()
{
	if (!emu_running) return 0;
	const uint32_t made = (uint32_t)(((uint64_t)(micros() - emu_t0_us) * 400ULL) / 1000000ULL);
	uint32_t waiting = made - emu_consumed;
	if (waiting > 32) { // the 32 entry FIFO overflowed: the oldest samples are lost, as in the sensor
		emu_consumed = made - 32;
		waiting = 32;
	}
	return (uint8_t)waiting;
}

void accel_read_sample(int16_t xyz[3])
{
	const float t = (float)emu_consumed / 400.f;
	const float s = emu_amp * sinf(2.f * (float)M_PI * emu_freq * t);
	xyz[0] = (int16_t)(0.8f * s + (float)((int)(emu_consumed * 7 % 5) - 2));
	xyz[1] = (int16_t)(0.5f * s + 13.f);
	xyz[2] = (int16_t)(256.f + 0.33f * s);
	emu_consumed++;
}

bool accel_overrun() { return false; }
void accel_stop() { emu_running = false; }
#endif //EMULATION

#endif //ACCEL_ADXL345
