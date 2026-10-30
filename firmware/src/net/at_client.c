/*
 * at_client.c - line oriented AT command client.
 */

#include <string.h>
#include "at_client.h"

static int at_is(const char *line, const char *text)
{
    return strcmp(line, text) == 0;
}

void at_client_init(at_client_t *client, char *buffer, size_t capacity,
                    uint32_t timeout_ms)
{
    memset(client, 0, sizeof(*client));
    client->buffer = buffer;
    client->capacity = capacity;
    client->timeout = timeout_ms;
    client->state = AT_IDLE;
    client->result = AT_OK;
}

int at_client_command(at_client_t *client, const char *command)
{
    if (client->state != AT_IDLE)
    {
        return -1;
    }
    client->pending = command;
    client->length = 0;
    client->overflow = 0;
    client->state = AT_COMMAND;
    client->result_count = 0;
    client->error_count = 0;
    return 0;
}

void at_client_rx(at_client_t *client, char c, uint32_t now_ms)
{
    if (client->state == AT_IDLE)
    {
        return;
    }

    client->deadline = now_ms;

    if (c == '\r' || c == '\n')
    {
        if (client->length == 0)
        {
            return;
        }
        client->buffer[client->length] = '\0';
        client->last_char = c;
        client->state = AT_RESULT;
        return;
    }

    if (client->length + 1 >= client->capacity)
    {
        client->overflow = 1;
        client->length = client->capacity - 1;
        client->buffer[client->length] = '\0';
        return;
    }

    client->buffer[client->length++] = c;
}

void at_client_tick(at_client_t *client, uint32_t now_ms)
{
    if (client->state == AT_IDLE)
    {
        return;
    }
    if ((uint32_t)(now_ms - client->deadline) > client->timeout)
    {
        client->state = AT_IDLE;
        client->result = AT_TIMEOUT;
        client->timeout_count++;
    }
}

at_event_t at_client_poll(at_client_t *client, const char **line)
{
    if (client->state == AT_IDLE)
    {
        return AT_NONE;
    }
    if (client->state != AT_RESULT)
    {
        return AT_NONE;
    }

    *line = client->buffer;

    if (at_is(client->buffer, "OK"))
    {
        client->result = AT_OK;
        client->result_count++;
        client->state = AT_IDLE;
        return AT_DONE;
    }
    if (at_is(client->buffer, "ERROR"))
    {
        client->result = AT_ERROR;
        client->error_count++;
        client->state = AT_IDLE;
        return AT_DONE;
    }
    if (at_is(client->buffer, "ABORTED"))
    {
        client->result = AT_ABORTED;
        client->error_count++;
        client->state = AT_IDLE;
        return AT_DONE;
    }

    client->state = AT_COMMAND;
    client->length = 0;
    return AT_LINE;
}

at_result_t at_client_result(const at_client_t *client)
{
    return client->result;
}
