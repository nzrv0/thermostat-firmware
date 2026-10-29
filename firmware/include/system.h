/*
 * system.h - system-level interfaces shared across the firmware.
 */

#ifndef SYSTEM_H
#define SYSTEM_H

#include <stdint.h>

/* Clock configuration: 8 MHz HSE, PLL x9 -> 72 MHz SYSCLK. */
#define HSE_FREQ_HZ          8000000UL
#define SYSCLK_HZ            72000000UL
#define APB1_TIMCLK_HZ       36000000UL
#define APB2_TIMCLK_HZ       72000000UL
#define CORE_CYCLES_PER_MS   (SYSCLK_HZ / 1000UL)

#define HSE_STARTUP_TIMEOUT  0x2000UL
#define PLL_STARTUP_TIMEOUT  0x2000UL

void SystemInit(void);
void system_reset(void);
void delay_cycles(uint32_t cycles);
void delay_ms(uint32_t ms);

#endif /* SYSTEM_H */
