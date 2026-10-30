/*
 * at_client.h - line oriented AT command client.
 *
 * The ESP-07S and the LoRaWAN modem are both driven with AT commands
 * over a UART. The firmware's command sets are recovered in
 * docs/subsystems.md; this client is the transport they share:
 *
 *   - commands are sent as "AT<command>\r\n"
 *   - responses are line based, terminated by "\r\n"
 *   - "+" lines are payload lines, "OK"/"ERROR" end the transaction
 *   - a prompt ('>') can be requested for payload transfers
 *
 * The transport is abstracted so the parser can be exercised on the host.
 */

#ifndef AT_CLIENT_H
#define AT_CLIENT_H

#include <stddef.h>
#include <stdint.h>

typedef enum
{
    AT_IDLE = 0,
    AT_COMMAND,
    AT_PAYLOAD,
    AT_RESULT
} at_state_t;

typedef enum
{
    AT_OK = 0,
    AT_ERROR = 1,
    AT_TIMEOUT = 2,
    AT_ABORTED = 3
} at_result_t;

typedef struct
{
    char   *buffer;        /* line buffer */
    size_t  capacity;
    size_t  length;
    char    last_char;     /* character that ended the line */
    int     overflow;      /* line did not fit */

    at_state_t state;
    at_result_t result;
    unsigned  result_count;
    unsigned  error_count;
    unsigned  timeout_count;

    const char *pending;   /* command waiting for its result */
    uint32_t    deadline;  /* millisecond stamp of the current wait */
    uint32_t    timeout;   /* milliseconds */
} at_client_t;

typedef enum
{
    AT_LINE = 0,       /* a complete line, no result yet */
    AT_DONE = 1,       /* the transaction finished */
    AT_NONE = 2        /* nothing to report */
} at_event_t;

void at_client_init(at_client_t *client, char *buffer, size_t capacity,
                    uint32_t timeout_ms);

/* Starts a transaction. Returns 0 when the command was accepted. */
int at_client_command(at_client_t *client, const char *command);

/* Feeds one received character. */
void at_client_rx(at_client_t *client, char c, uint32_t now_ms);

/* Polls for the timeout. */
void at_client_tick(at_client_t *client, uint32_t now_ms);

/* Takes the next event. */
at_event_t at_client_poll(at_client_t *client, const char **line);

/* The result of the last transaction. */
at_result_t at_client_result(const at_client_t *client);

#endif /* AT_CLIENT_H */
