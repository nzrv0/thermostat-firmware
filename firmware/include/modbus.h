/*
 * modbus.h - the Modbus register map and server core.
 *
 * The firmware publishes its points in a table at 0x08090528-0x08090990:
 * four word descriptors {name, label, value, handler}. The names are
 * recovered (see docs/subsystems.md) and the handlers read the live
 * values - the relay handlers, for instance, read one GPIO pin each.
 *
 * The rebuild keeps the same point names and drives them from the board
 * state, and serves them over both transports: Modbus TCP (MBAP header)
 * and Modbus RTU (CRC-16 framed).
 */

#ifndef MODBUS_H
#define MODBUS_H

#include <stdint.h>

#define MODBUS_MAX_POINTS     20
#define MODBUS_NAME_MAX       24
#define MODBUS_TCP_HEADER     7
#define MODBUS_PDU_MAX        253

typedef enum
{
    MODBUS_POINT_HOLDING = 0,
    MODBUS_POINT_INPUT = 1,
    MODBUS_POINT_COIL = 2,
    MODBUS_POINT_DISCRETE = 3
} modbus_point_kind_t;

typedef struct
{
    const char         *name;
    const char         *label;
    modbus_point_kind_t kind;
    uint16_t            address;     /* Modbus address */
    uint16_t            scale;       /* value = raw / scale */
    uint16_t            raw;         /* the raw value the point holds */
} modbus_point_t;

/* The point table. */
extern modbus_point_t g_modbus_points[MODBUS_MAX_POINTS];
extern const uint16_t g_modbus_point_count;

/* Looks a point up by address within a kind. */
const modbus_point_t *modbus_find(uint16_t address, modbus_point_kind_t kind);

/* Builds a Modbus TCP response for a request. Returns the response
 * length, or 0 when the request is malformed. */
int modbus_tcp_process(const uint8_t *request, int request_length,
                        uint8_t *response, int response_capacity);

/* Builds a Modbus RTU response (with the CRC-16) for a request. */
int modbus_rtu_process(const uint8_t *request, int request_length,
                        uint8_t *response, int response_capacity);

/* The CRC-16 the RTU framing uses. */
uint16_t modbus_crc16(const uint8_t *data, int length);

#endif /* MODBUS_H */
