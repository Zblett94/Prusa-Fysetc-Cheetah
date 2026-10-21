//! @file
//! @brief util/atomic.h replacement (ATOMIC_BLOCK / NONATOMIC_BLOCK) for Cortex-M.
#pragma once

#include <stdint.h>
#include <stm32_def.h>
#undef CRC // CMSIS CRC peripheral macro, clashes with the MMU protocol code

static inline uint32_t __iSeiRetVal(void) { __enable_irq(); return 1; }
static inline uint32_t __iCliRetVal(void) { __disable_irq(); return 1; }
static inline void __iRestore(const uint32_t *__s) { __set_PRIMASK(*__s); __asm__ volatile ("" ::: "memory"); }
static inline void __iSeiParam(const uint32_t *__s) { (void)__s; __enable_irq(); __asm__ volatile ("" ::: "memory"); }
static inline void __iCliParam(const uint32_t *__s) { (void)__s; __disable_irq(); __asm__ volatile ("" ::: "memory"); }

#define ATOMIC_BLOCK(type) for ( type, __ToDo = __iCliRetVal(); __ToDo ; __ToDo = 0 )
#define NONATOMIC_BLOCK(type) for ( type, __ToDo = __iSeiRetVal(); __ToDo ; __ToDo = 0 )

#define ATOMIC_RESTORESTATE uint32_t primask_save __attribute__((__cleanup__(__iRestore))) = __get_PRIMASK()
#define ATOMIC_FORCEON uint32_t primask_save __attribute__((__cleanup__(__iSeiParam))) = 0
#define NONATOMIC_RESTORESTATE uint32_t primask_save __attribute__((__cleanup__(__iRestore))) = __get_PRIMASK()
#define NONATOMIC_FORCEOFF uint32_t primask_save __attribute__((__cleanup__(__iCliParam))) = 0
