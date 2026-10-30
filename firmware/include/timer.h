/*
 * timer.h - the timers the firmware uses.
 *
 * TIM3 and TIM5 are referenced by the application (fan, stage timers),
 * TIM8 and TIM9..TIM11 by the UI and the tick. The firmware runs them
 * as free running or one pulse timers.
 */

#ifndef TIMER_H
#define TIMER_H

#include <stdint.h>
#include "stm32f1xx.h"

typedef struct
{
    TIM_TypeDef *port;
    uint32_t     peripheral_clock;
    uint32_t     tick_hz;
    uint32_t     prescaler;
    uint32_t     period;
    uint32_t     count;
} timer_t;

typedef enum
{
    TIMER_FREE_RUNNING = 0,
    TIMER_ONE_PULSE = 1
} timer_mode_t;

/* Starts a timer counting at tick_hz. */
int timer_init(timer_t *timer, TIM_TypeDef *port, uint32_t peripheral_clock,
               uint32_t tick_hz, timer_mode_t mode);

/* The current count. */
uint32_t timer_count(const timer_t *timer);

/* Microseconds since the timer was started. */
uint32_t timer_micros(const timer_t *timer);

#endif /* TIMER_H */
