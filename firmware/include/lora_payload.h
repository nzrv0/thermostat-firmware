/*
 * lora_payload.h - the LoRaWAN uplink payload.
 *
 * What the firmware proves:
 *
 *   - The uplink is sent with the Dragino command "AT+CTXP=%d"
 *     (0x08043e58), where the parameter is the payload length; the
 *     payload itself follows the command.
 *   - The modem is addressed with " AT+DTRX=1,2,%d," (0x08043447), so
 *     the payload is sent as a hexadecimal string.
 *   - The modem's answers are parsed with "%02x" and "%04x", so the
 *     payload is a byte string, not text.
 *   - The downlink arrives as "OK+RECV" (0x08043efc) followed by
 *     " OK+RECV:" (0x08043447 area) and is hexadecimal.
 *
 * The *field layout* of the uplink is not recoverable from the image:
 * the payload is built at run time from the telemetry document and no
 * static table describes it. This codec therefore defines a compact
 * binary layout for the same telemetry the MQTT document carries. It
 * is a rebuild choice, documented as such, in the same way the NTC
 * temperature curve is (see docs/hvac-spec.md).
 */

#ifndef LORA_PAYLOAD_H
#define LORA_PAYLOAD_H

#include <stdint.h>

#define LORA_PAYLOAD_MAX 64

/* The telemetry the uplink carries. */
typedef struct
{
    int16_t  room_temperature;   /* tenths of a degree Celsius */
    int16_t  humidity;           /* tenths of a percent */
    int16_t  heat_setpoint;      /* tenths of a degree Celsius */
    int16_t  cool_setpoint;      /* tenths of a degree Celsius */
    uint8_t  mode;               /* the operating mode */
    uint8_t  flags;              /* the published flags */
    uint8_t  relays;             /* bit 0 W1, 1 W2, 2 Y1, 3 Y2, 4 fan */
    uint32_t uptime;             /* seconds */
} lora_telemetry_t;

/* The header of the encoded payload. */
#define LORA_VERSION 1

/* Encodes the telemetry. Returns the number of bytes written, or -1
 * when the buffer is too small. */
int lora_payload_encode(uint8_t *buffer, uint32_t capacity,
                        const lora_telemetry_t *telemetry);

/* Decodes a payload. Returns 0 on success, -1 when the payload is
 * malformed. */
int lora_payload_decode(const uint8_t *buffer, uint32_t length,
                        lora_telemetry_t *telemetry);

/* The hexadecimal form the "AT+CTXP=" command carries. The caller
 * supplies the buffer; `hex` is the hex digits without the 0x prefix
 * and without a terminator. Returns the number of hex digits written,
 * or -1 when the buffer is too small. */
int lora_payload_hex(const uint8_t *payload, uint32_t length,
                     char *hex, uint32_t capacity);

#endif /* LORA_PAYLOAD_H */
