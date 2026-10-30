/*
 * scheduler.h - tick and window message model.
 *
 * The original is a bare-metal super loop driven by a tick, with a
 * window message layer on top: the firmware carries message names such
 * as "WM_AT_TCP_SEND_TEXT" and "WM_AT_TCP_CLOSE" (0x0806B0BC and
 * 0x0806B0F0). This module provides the same shape: a millisecond tick
 * plus a queue of messages drained by the main loop.
 */

#ifndef SCHEDULER_H
#define SCHEDULER_H

#include <stdint.h>

typedef enum
{
    WM_NONE = 0,
    WM_TICK,
    WM_AT_TX,
    WM_AT_RX,
    WM_AT_TCP_SEND_TEXT,
    WM_AT_TCP_CLOSE,
    WM_SENSOR_POLL,
    WM_HVAC_EVALUATE,
    WM_UI_REDRAW,
    WM_COUNT
} window_message_t;

typedef struct
{
    window_message_t message;
    uint32_t         param;
    const char     *text;
} scheduler_message_t;

#define SCHEDULER_QUEUE_SIZE 16

typedef void (*scheduler_handler_t)(const scheduler_message_t *message,
                                    void *context);

typedef struct
{
    uint32_t              ticks;
    scheduler_message_t   queue[SCHEDULER_QUEUE_SIZE];
    uint16_t              head;
    uint16_t              tail;
    uint32_t              dropped;
    scheduler_handler_t    handler;
    void                 *context;
} scheduler_t;

void scheduler_init(scheduler_t *scheduler);

/* Called from the tick interrupt. */
void scheduler_tick(scheduler_t *scheduler);

/* Posts a message; returns 0 when it was queued. */
int scheduler_post(scheduler_t *scheduler, window_message_t message,
                    uint32_t param, const char *text);

/* Takes the next message; returns 0 when the queue is empty. */
int scheduler_next(scheduler_t *scheduler, scheduler_message_t *out);

/* Drains the queue through the handler. */
void scheduler_run(scheduler_t *scheduler);

/* The system scheduler the tick interrupt updates. */
scheduler_t *scheduler_system(void);

/* Starts the millisecond tick on SysTick at the given core frequency. */
void scheduler_start(uint32_t core_hz);

#endif /* SCHEDULER_H */
