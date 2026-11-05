#include "adc.h"
#include <stdio.h>
#include <avr/io.h>
#include <avr/interrupt.h>
#include <avr/pgmspace.h>
#include <string.h>
#include "pins.h"
#ifndef __AVR__
#include <Arduino.h>
#endif

static uint8_t adc_count; //used for oversampling
static uint8_t adc_channel_idx; //bitmask index
volatile uint8_t adc_channel; //regular index
volatile uint16_t adc_values[ADC_CHAN_CNT];

static void adc_reset();
static void adc_setmux(uint8_t ch);

#ifdef __AVR__

void adc_init()
{
	puts_P(PSTR("adc_init"));
    DIDR0 = ((ADC_CHAN_MSK & ADC_DIDR_MSK) & 0xff); //disable digital inputs PORTF
    DIDR2 = ((ADC_CHAN_MSK & ADC_DIDR_MSK) >> 8); //disable digital inputs PORTK
    ADMUX |= (1 << REFS0); //use AVCC as reference

    //enable ADC, set prescaler/128, enable interrupt
    ADCSRA = (1 << ADEN) | (1 << ADPS2) | (1 << ADPS1) | (1 << ADPS0) | (1 << ADIF) | (1 << ADIE);
}

static void adc_setmux(uint8_t ch)
{
	ch &= 0x0f;
	if (ch & 0x08) ADCSRB |= (1 << MUX5);
	else ADCSRB &= ~(1 << MUX5);
	ADMUX = (ADMUX & ~(0x07)) | (ch & 0x07);
}

#define ADC_START_CONVERSION() (ADCSRA |= (1 << ADSC))
#define ADC_STOP_CONVERSION()  (ADCSRA &= ~(1 << ADSC))
#define ADC_RESULT()           (ADC)

#else //__AVR__

// STM32 ADC1, one regular conversion per interrupt, exactly like the AVR ADC ISR.
// The 12 bit result is scaled to 10 bits: the thermistor dividers are ratiometric
// (pulled up to the ADC reference) so all the AVR raw value tables stay valid.

static void adc_analog_pins()
{
	for (uint8_t i = 0; i < 32; i++) {
		if (ADC_CHAN_MSK & (1UL << i)) {
			const uint32_t pin = adc_channel_pin(i);
			if (pin >= NUM_DIGITAL_PINS) continue; // internal channel
			GPIO_TypeDef *port = _FIO_PORT(pin);
			const uint32_t b = fastio::pin_bit(pin);
			port->PUPDR &= ~(3UL << (b * 2));
			port->MODER |= (3UL << (b * 2));
		}
	}
}

#if defined(STM32G0xx)

void adc_init()
{
	puts_P(PSTR("adc_init"));
	adc_analog_pins();
	__HAL_RCC_ADC_CLK_ENABLE();
	ADC1->CR = 0;
	ADC1->CFGR2 = (2UL << ADC_CFGR2_CKMODE_Pos); // PCLK/4 = 16 MHz, synchronous
	ADC1->CR = ADC_CR_ADVREGEN;
	delayMicroseconds(25); // tADCVREG_STUP
	ADC1->CR |= ADC_CR_ADCAL;
	for (uint32_t i = 0; i < 100000 && (ADC1->CR & ADC_CR_ADCAL); i++) {}
	delayMicroseconds(5);
	ADC1->CFGR1 = 0; // 12 bit, right aligned, single conversion, software trigger
	ADC1_COMMON->CCR |= ADC_CCR_TSEN | ADC_CCR_VREFEN; // temperature sensor and VREFINT channels
	delayMicroseconds(120); // temperature sensor start-up time
	ADC1->SMPR = 7UL << ADC_SMPR_SMP1_Pos; // 160.5 cycles: high impedance thermistor dividers
	ADC1->ISR = ADC_ISR_ADRDY;
	ADC1->CR |= ADC_CR_ADEN;
	for (uint32_t i = 0; i < 100000 && !(ADC1->ISR & ADC_ISR_ADRDY); i++) {}
	ADC1->IER = ADC_IER_EOCIE;
	NVIC_SetPriority(ADC1_COMP_IRQn, HAL_PRIO_SOFTPWM);
	NVIC_EnableIRQ(ADC1_COMP_IRQn);
}

static void adc_setmux(uint8_t ch)
{
	// CHSELR may only be written when no conversion is ongoing (ADSTART is cleared by
	// the hardware at the end of the single conversion sequence)
	for (uint16_t i = 0; i < 1000 && (ADC1->CR & ADC_CR_ADSTART); i++) {}
	ADC1->ISR = ADC_ISR_CCRDY | ADC_ISR_EOS;
	ADC1->CHSELR = 1UL << ch;
	for (uint16_t i = 0; i < 1000 && !(ADC1->ISR & ADC_ISR_CCRDY); i++) {} // a few ADC clocks
}

#define ADC_START_CONVERSION() (ADC1->CR |= ADC_CR_ADSTART)
#define ADC_STOP_CONVERSION()  do { if (ADC1->CR & ADC_CR_ADSTART) { ADC1->CR |= ADC_CR_ADSTP; for (uint16_t _i = 0; _i < 1000 && (ADC1->CR & ADC_CR_ADSTP); _i++) {} } } while (0)
#define ADC_RESULT()           (ADC1->DR >> 2)
#define ADC_EOC()              (ADC1->ISR & ADC_ISR_EOC)
#define ADC_IRQ_HANDLER        ADC1_COMP_IRQHandler

#elif defined(STM32F4xx)

void adc_init()
{
	puts_P(PSTR("adc_init"));
	adc_analog_pins();
	__HAL_RCC_ADC1_CLK_ENABLE();
	// ADC clock PCLK2 / 4 = 22.5 MHz (max. 36 MHz), temperature sensor and VREFINT on
	ADC123_COMMON->CCR = (ADC123_COMMON->CCR & ~ADC_CCR_ADCPRE) | ADC_CCR_ADCPRE_0 | ADC_CCR_TSVREFE;
	ADC1->CR1 = ADC_CR1_EOCIE; // 12 bit, one channel
	ADC1->CR2 = 0;             // single conversion, software trigger, right aligned
	// 480 cycles (21 us) on every channel: high impedance thermistor dividers, and the
	// temperature sensor needs >= 10 us
	ADC1->SMPR1 = 0x07FFFFFF;
	ADC1->SMPR2 = 0x3FFFFFFF;
	ADC1->SQR1 = 0;            // one conversion
	ADC1->CR2 |= ADC_CR2_ADON;
	delayMicroseconds(10);     // tSTAB (and the temperature sensor start-up)
	ADC1->SR = 0;
	NVIC_SetPriority(ADC_IRQn, HAL_PRIO_SOFTPWM);
	NVIC_EnableIRQ(ADC_IRQn);
}

static void adc_setmux(uint8_t ch)
{
	ADC1->SQR3 = ch; // first (only) conversion of the sequence
}

#define ADC_START_CONVERSION() (ADC1->CR2 |= ADC_CR2_SWSTART)
// a single conversion can not be aborted: let it end and drop its result
#define ADC_STOP_CONVERSION()  do { for (uint16_t _i = 0; _i < 2000 && (ADC1->SR & ADC_SR_STRT) && !(ADC1->SR & ADC_SR_EOC); _i++) {} (void)ADC1->DR; ADC1->SR = 0; } while (0)
#define ADC_RESULT()           (ADC1->DR >> 2)
#define ADC_EOC()              (ADC1->SR & ADC_SR_EOC)
#define ADC_IRQ_HANDLER        ADC_IRQHandler

#endif // STM32 family

#endif //__AVR__

static void adc_reset()
{
    static const uint8_t first_channel_idx = __builtin_ctz(ADC_CHAN_MSK);
    static_assert((1 << first_channel_idx) & ADC_CHAN_MSK);

    ADC_STOP_CONVERSION(); //stop conversion just in case
    adc_count = 0;
    adc_channel = 0;
    adc_channel_idx = first_channel_idx;
    adc_setmux(adc_channel_idx);
    memset((void*)adc_values, 0, sizeof(adc_values));
}

#ifdef EMULATION
// Renode test build: the samples come from the thermal simulation in temperature.cpp
extern uint16_t emulation_adc_sample(uint8_t ch);
#ifdef ADC_CALLBACK
extern void ADC_CALLBACK();
#endif
void adc_start_cycle() {
	adc_reset();
	for (uint8_t ch = 0, i = 0; ch < 32; ch++)
		if (ADC_CHAN_MSK & (1UL << ch))
			adc_values[i++] = emulation_adc_sample(ch) * ADC_OVRSAMPL;
	adc_channel = ADC_CHAN_CNT;
#ifdef ADC_CALLBACK
	ADC_CALLBACK();
#endif
}
#else //EMULATION
void adc_start_cycle() {
	adc_reset();
	ADC_START_CONVERSION(); //start conversion
}
#endif //EMULATION

#ifdef ADC_CALLBACK
extern void ADC_CALLBACK();
#endif //ADC_CALLBACK

#ifdef __AVR__
ISR(ADC_vect)
#else
extern "C" void ADC_IRQ_HANDLER(void)
#endif
{
#ifndef __AVR__
    if (!ADC_EOC()) return;
#endif
    adc_values[adc_channel] += ADC_RESULT();
    if (++adc_count == ADC_OVRSAMPL)
    {
        // go to the next channel
        if (++adc_channel == ADC_CHAN_CNT) {
#ifdef ADC_CALLBACK
            ADC_CALLBACK();
#endif
            return; // do not start the next measurement since there are no channels remaining
        }

        // find the next channel
        while (++adc_channel_idx) {
            if (ADC_CHAN_MSK & (1UL << adc_channel_idx)) {
                adc_setmux(adc_channel_idx);
                adc_count = 0;
                break;
            }
        }
    }
    ADC_START_CONVERSION(); //start conversion
}
