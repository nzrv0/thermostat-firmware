/*
 * lora_payload.c - the LoRaWAN uplink payload.
 *
 * See lora_payload.h for the evidence and for what is a rebuild choice.
 */

#include <string.h>
#include "lora_payload.h"

/* The layout:
 *
 *   offset  size  field
 *   0       1     version
 *   1       1     mode
 *   2       1     flags
 *   3       1     relays
 *   4       2     room temperature, tenths of a degree, little endian
 *   6       2     humidity, tenths of a percent
 *   8       2     heat setpoint, tenths of a degree
 *   10      2     cool setpoint, tenths of a degree
 *   12      4     uptime in seconds, little endian
 *
 * Sixteen bytes, which fits every LoRaWAN data rate the firmware
 * offers (the smallest is SF12, 51 bytes).
 */
#define LORA_PAYLOAD_SIZE 16

static void put_u16(uint8_t *p, uint16_t v)
{
    p[0] = (uint8_t)(v & 0xFF);
    p[1] = (uint8_t)(v >> 8);
}

static void put_u32(uint8_t *p, uint32_t v)
{
    p[0] = (uint8_t)(v & 0xFF);
    p[1] = (uint8_t)((v >> 8) & 0xFF);
    p[2] = (uint8_t)((v >> 16) & 0xFF);
    p[3] = (uint8_t)((v >> 24) & 0xFF);
}

static uint16_t get_u16(const uint8_t *p)
{
    return (uint16_t)((uint16_t)p[0] | ((uint16_t)p[1] << 8));
}

static uint32_t get_u32(const uint8_t *p)
{
    return (uint32_t)p[0] | ((uint32_t)p[1] << 8) |
           ((uint32_t)p[2] << 16) | ((uint32_t)p[3] << 24);
}

int lora_payload_encode(uint8_t *buffer, uint32_t capacity,
                        const lora_telemetry_t *telemetry)
{
    if (buffer == 0 || telemetry == 0)
    {
        return -1;
    }
    if (capacity < LORA_PAYLOAD_SIZE)
    {
        return -1;
    }

    buffer[0] = LORA_VERSION;
    buffer[1] = telemetry->mode;
    buffer[2] = telemetry->flags;
    buffer[3] = telemetry->relays;
    put_u16(buffer + 4, (uint16_t)telemetry->room_temperature);
    put_u16(buffer + 6, (uint16_t)telemetry->humidity);
    put_u16(buffer + 8, (uint16_t)telemetry->heat_setpoint);
    put_u16(buffer + 10, (uint16_t)telemetry->cool_setpoint);
    put_u32(buffer + 12, telemetry->uptime);

    return LORA_PAYLOAD_SIZE;
}

int lora_payload_decode(const uint8_t *buffer, uint32_t length,
                        lora_telemetry_t *telemetry)
{
    if (buffer == 0 || telemetry == 0)
    {
        return -1;
    }
    if (length < LORA_PAYLOAD_SIZE)
    {
        return -1;
    }
    if (buffer[0] != LORA_VERSION)
    {
        return -1;
    }

    telemetry->mode = buffer[1];
    telemetry->flags = buffer[2];
    telemetry->relays = buffer[3];
    telemetry->room_temperature = (int16_t)get_u16(buffer + 4);
    telemetry->humidity = (int16_t)get_u16(buffer + 6);
    telemetry->heat_setpoint = (int16_t)get_u16(buffer + 8);
    telemetry->cool_setpoint = (int16_t)get_u16(buffer + 10);
    telemetry->uptime = get_u32(buffer + 12);

    return 0;
}

int lora_payload_hex(const uint8_t *payload, uint32_t length,
                     char *hex, uint32_t capacity)
{
    static const char digits[] = "0123456789abcdef";
    uint32_t i;

    if (payload == 0 || hex == 0)
    {
        return -1;
    }
    if (capacity < length * 2u + 1u)
    {
        return -1;
    }
    for (i = 0; i < length; i++)
    {
        hex[i * 2] = digits[(payload[i] >> 4) & 0x0F];
        hex[i * 2 + 1] = digits[payload[i] & 0x0F];
    }
    hex[length * 2] = '\0';
    return (int)(length * 2u);
}
