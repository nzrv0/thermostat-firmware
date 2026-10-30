/*
 * timer.c - the timers the firmware uses.
 */

#include "timer.h"

#define TIM_CR1_CEN   (1UL << 0)
#define TIM_CR1_OPM   (1UL << 3)
#define TIM_CR1_ARPE  (1UL << 7)
#define TIM_EGR_UG    (1UL << 0)

int timer_init(timer_t *timer, TIM_TypeDef *port, uint32_t peripheral_clock,
               uint32_t tick_hz, timer_mode_t mode)
{
    uint32_t divider;

    if (timer == 0 || port == 0 || tick_hz == 0)
    {
        return -1;
    }

    timer->port = port;
    timer->peripheral_clock = peripheral_clock;
    timer->tick_hz = tick_hz;
    timer->prescaler = 0;
    timer->period = 0xFFFFFFFFUL;
    timer->count = 0;

    port->CR1 = 0;
    port->CR2 = 0;
    port->DIER = 0;
    port->SR = 0;

    divider = peripheral_clock / tick_hz;
    if (divider < 1)
    {
        divider = 1;
    }
    timer->prescaler = divider - 1;

    port->PSC = timer->prescaler;
    port->ARR = timer->period;
    port->EGR = TIM_EGR_UG;
    port->CNT = 0;

    if (mode == TIMER_ONE_PULSE)
    {
        port->CR1 = TIM_CR1_CEN | TIM_CR1_OPM | TIM_CR1_ARPE;
    }
    else
    {
        port->CR1 = TIM_CR1_CEN | TIM_CR1_ARPE;
    }
    return 0;
}

uint32_t timer_count(const timer_t *timer)
{
    return timer->port->CNT;
}

uint32_t timer_micros(const timer_t *timer)
{
    uint64_t ticks = timer->port->CNT;

    if (timer->tick_hz == 0)
    {
        return 0;
    }
    return (uint32_t)((ticks * 1000000ULL) / timer->tick_hz);
}
