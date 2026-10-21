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

// STM32G0 ADC1, one regular conversion per interrupt, exactly like the AVR ADC ISR.
// The 12 bit result is scaled to 10 bits: the thermistor dividers are ratiometric
// (pulled up to the ADC reference) so all the AVR raw value tables stay valid.

void adc_init()
{
	puts_P(PSTR("adc_init"));
	// analog mode on the used pins
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

	__HAL_RCC_ADC_CLK_ENABLE();
	ADC1->CR = 0;
	ADC1->CFGR2 = (2UL << ADC_CFGR2_CKMODE_Pos); // PCLK/4 = 16 MHz, synchronous
	ADC1->CR = ADC_CR_ADVREGEN;
	delayMicroseconds(25); // tADCVREG_STUP
	ADC1->CR |= ADC_CR_ADCAL;
	while (ADC1->CR & ADC_CR_ADCAL) {}
	delayMicroseconds(5);
	ADC1->CFGR1 = 0; // 12 bit, right aligned, single conversion, software trigger
	ADC1_COMMON->CCR |= ADC_CCR_TSEN | ADC_CCR_VREFEN; // temperature sensor and VREFINT channels
	delayMicroseconds(120); // temperature sensor start-up time
	ADC1->SMPR = 7UL << ADC_SMPR_SMP1_Pos; // 160.5 cycles: high impedance thermistor dividers
	ADC1->ISR = ADC_ISR_ADRDY;
	ADC1->CR |= ADC_CR_ADEN;
	while (!(ADC1->ISR & ADC_ISR_ADRDY)) {}
	ADC1->IER = ADC_IER_EOCIE;
	NVIC_SetPriority(ADC1_COMP_IRQn, HAL_PRIO_SOFTPWM);
	NVIC_EnableIRQ(ADC1_COMP_IRQn);
}

static void adc_setmux(uint8_t ch)
{
	// CHSELR may only be written when no conversion is ongoing
	ADC1->ISR = ADC_ISR_CCRDY;
	ADC1->CHSELR = 1UL << ch;
	while (!(ADC1->ISR & ADC_ISR_CCRDY)) {}
}

#define ADC_START_CONVERSION() (ADC1->CR |= ADC_CR_ADSTART)
#define ADC_STOP_CONVERSION()  do { if (ADC1->CR & ADC_CR_ADSTART) { ADC1->CR |= ADC_CR_ADSTP; while (ADC1->CR & ADC_CR_ADSTP) {} } } while (0)
#define ADC_RESULT()           (ADC1->DR >> 2)

#endif //__AVR__

static void adc_reset()
{
    static const uint8_t first_channel_idx = 0;
    static_assert((1 << first_channel_idx) & ADC_CHAN_MSK);

    ADC_STOP_CONVERSION(); //stop conversion just in case
    adc_count = 0;
    adc_channel = 0;
    adc_channel_idx = first_channel_idx;
    adc_setmux(adc_channel_idx);
    memset((void*)adc_values, 0, sizeof(adc_values));
}

void adc_start_cycle() {
	adc_reset();
	ADC_START_CONVERSION(); //start conversion
}

#ifdef ADC_CALLBACK
extern void ADC_CALLBACK();
#endif //ADC_CALLBACK

#ifdef __AVR__
ISR(ADC_vect)
#else
extern "C" void ADC1_COMP_IRQHandler(void)
#endif
{
#ifndef __AVR__
    if (!(ADC1->ISR & ADC_ISR_EOC)) return;
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
