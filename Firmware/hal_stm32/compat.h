//! @file
//! @brief Compatibility helpers for code written against avr-libc and the AVR Arduino core
#pragma once

#ifndef __AVR__

#include <stdint.h>
#include <stdio.h>
#include <avr/pgmspace.h>

// avr-libc stdio extensions (flash and RAM share one address space on the STM32)
#ifndef puts_P
#define puts_P(s) puts(s)
#endif
#ifndef fputs_P
#define fputs_P(s, f) fputs((s), (f))
#endif
#ifndef fprintf_P
#define fprintf_P(f, ...) fprintf((f), __VA_ARGS__)
#endif
// the STM32 core versions do not accept arrays (typeof(array) initializer)
#undef pgm_read_float
#define pgm_read_float(addr) (*(const float *)(addr))
#undef pgm_read_ptr
#define pgm_read_ptr(addr) (*(void * const *)(addr))

// avr-libc math.h extension
static inline float square(float x) { return x * x; }

// util/delay.h
#ifdef __cplusplus
extern "C" {
#endif
void hal_delay_us(uint32_t us);
#ifdef __cplusplus
}
#endif
#ifndef _delay_us
#define _delay_us(us) hal_delay_us(us)
#define _delay_ms(ms) hal_delay_us((uint32_t)((ms) * 1000))
#endif

#ifdef __cplusplus
#include <algorithm>

// The AVR Arduino core defines min()/max() as macros accepting mixed types,
// STM32duino uses std::min/std::max which require identical types.
// std::min/max stay the better match when both arguments have the same type.
template <class A, class B>
static inline constexpr auto min(const A a, const B b) -> decltype(a + b) { return a < b ? a : b; }
template <class A, class B>
static inline constexpr auto max(const A a, const B b) -> decltype(a + b) { return a > b ? a : b; }
#endif //__cplusplus

#endif //!__AVR__
