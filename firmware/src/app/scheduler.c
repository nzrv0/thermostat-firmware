/*
 * scheduler.c - tick and window message model.
 */

#include <string.h>
#include "scheduler.h"
#include "stm32f1xx.h"

static scheduler_t g_scheduler;

void scheduler_init(scheduler_t *scheduler)
{
    memset(scheduler, 0, sizeof(*scheduler));
}

scheduler_t *scheduler_system(void)
{
    return &g_scheduler;
}

void scheduler_start(uint32_t core_hz)
{
    uint32_t reload = (core_hz / 1000u) - 1u;

    if (reload == 0)
    {
        reload = 1;
    }
    SysTick->LOAD = reload;
    SysTick->VAL = 0;
    SysTick->CTRL = 7;   /* enable, tick interrupt, processor clock */
}

void scheduler_tick(scheduler_t *scheduler)
{
    scheduler->ticks++;
}

int scheduler_post(scheduler_t *scheduler, window_message_t message,
                   uint32_t param, const char *text)
{
    uint16_t next = (uint16_t)((scheduler->head + 1) % SCHEDULER_QUEUE_SIZE);

    if (next == scheduler->tail)
    {
        scheduler->dropped++;
        return -1;
    }

    scheduler->queue[scheduler->head].message = message;
    scheduler->queue[scheduler->head].param = param;
    scheduler->queue[scheduler->head].text = text;
    scheduler->head = next;
    return 0;
}

int scheduler_next(scheduler_t *scheduler, scheduler_message_t *out)
{
    if (scheduler->tail == scheduler->head)
    {
        return 0;
    }

    *out = scheduler->queue[scheduler->tail];
    scheduler->tail = (uint16_t)((scheduler->tail + 1) % SCHEDULER_QUEUE_SIZE);
    return 1;
}

void scheduler_run(scheduler_t *scheduler)
{
    scheduler_message_t message;

    while (scheduler_next(scheduler, &message))
    {
        if (scheduler->handler != 0)
        {
            scheduler->handler(&message, scheduler->context);
        }
    }
}
