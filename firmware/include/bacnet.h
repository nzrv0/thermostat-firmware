/*
 * bacnet.h - the BACnet servers.
 *
 * The firmware offers two BACnet servers, selected by the menu entries
 * "Bacnet IP" (0x0809275c) and "Bacnet MS/TP" (0x08092768), and started
 * by the message "bacnetip servo start..." (0x0806b098).
 *
 * What the image establishes:
 *
 *   - The point registry at 0x08090528 publishes the objects the
 *     servers expose: the room temperature, the humidity, the heat and
 *     cool setpoints, the hold and holiday times, the setpoint limits,
 *     the five relay states and the published flags.
 *   - The same registry is used by the Modbus TCP and RTU servers, so
 *     one object table serves all three protocols.
 *   - BACnet/IP is BACnet over UDP: the device is reachable on the
 *     standard BACnet/IP port and the NPDU version byte is 1.
 *   - MS/TP is BACnet over the RS-485 line, with the master taking the
 *     bus and the frame carrying a 0x55 preamble, a length byte and a
 *     CRC.
 *
 * The object table itself is expressed in the rebuild as a list of
 * objects with a type, an instance and a property accessor, which is
 * the shape the registry already has.
 */

#ifndef BACNET_H
#define BACNET_H

#include <stdint.h>

/* BACnet object types the device publishes. */
#define BAC_OBJ_ANALOG_VALUE   0
#define BAC_OBJ_BINARY_VALUE   5
#define BAC_OBJ_MULTI_VALUE   19

/* The property identifiers the servers answer. */
#define BAC_PROP_OBJECT_IDENTIFIER  75
#define BAC_PROP_OBJECT_NAME        58
#define BAC_PROP_OBJECT_TYPE        59
#define BAC_PROP_PRESENT_VALUE      85
#define BAC_PROP_STATUS_FLAGS      89
#define BAC_PROP_UNITS              92
#define BAC_PROP_OUT_OF_SERVICE     81

/* The units the published points use. */
#define BAC_UNITS_NONE            0
#define BAC_UNITS_DEGREES_CELSIUS 62
#define BAC_UNITS_PERCENT_RELATIVE_HUM 83
#define BAC_UNITS_SECONDS         10
#define BAC_UNITS_MINUTES          11

/* A published object. */
typedef struct
{
    uint32_t object_type;    /* BAC_OBJ_* */
    uint32_t instance;
    const char *name;        /* the "Actual_Room_temperature" style key */
    const char *label;       /* the "Room temperature" style text */
    int32_t    *value;       /* the live value, or NULL for a literal */
    int32_t     literal;     /* used when value is NULL */
    uint32_t    units;
    uint32_t    writable;
} bacnet_object_t;

/* The transport a server runs on. */
typedef enum
{
    BACNET_TRANSPORT_IP = 0,
    BACNET_TRANSPORT_MSTP
} bacnet_transport_t;

/* A BACnet/IP server. */
typedef struct
{
    uint16_t port;                 /* the UDP port, 4780 by default */
    const bacnet_object_t *objects;
    uint32_t object_count;
    uint32_t device_instance;
    const char *vendor_name;
    const char *model_name;
    uint32_t vendor_id;
} bacnet_ip_t;

/* An MS/TP master. */
typedef struct
{
    uint8_t  master_address;       /* 0..250 */
    uint32_t baud;
    uint8_t  parity;               /* 0: none, 1: even, 2: odd */
    uint8_t  data_bits;            /* 8 */
    uint8_t  stop_bits;            /* 1 */
    const bacnet_object_t *objects;
    uint32_t object_count;
    uint32_t device_instance;
    const char *vendor_name;
    const char *model_name;
    uint32_t vendor_id;
} bacnet_mstp_t;

/* ------------------------------------------------------------------ */
/* The object table                                                    */
/* ------------------------------------------------------------------ */

/* Finds an object by instance. Returns NULL when there is none. */
const bacnet_object_t *bacnet_find_object(const bacnet_object_t *objects,
                                           uint32_t count,
                                           uint32_t instance);

/* Reads an object's present value. */
int32_t bacnet_object_value(const bacnet_object_t *obj);

/* Writes an object's present value. Returns 0 on success. */
int bacnet_object_write(bacnet_object_t *obj, int32_t value);

/* ------------------------------------------------------------------ */
/* The encoding                                                         */
/* ------------------------------------------------------------------ */

/* Encodes a ReadProperty request answer.
 *
 *   buffer   the destination
 *   capacity its size
 *   object   the object that was read
 *   property the property identifier
 *   value    the value, for BAC_PROP_PRESENT_VALUE
 *
 * Returns the number of bytes written, or -1. */
int bacnet_encode_read_property(uint8_t *buffer, uint32_t capacity,
                                const bacnet_object_t *object,
                                uint32_t property, int32_t value);

/* Encodes the device object property list, used by ReadProperty on the
 * device object (instance 0xFFFFFFFF). */
int bacnet_encode_device_property(uint8_t *buffer, uint32_t capacity,
                                   uint32_t property,
                                   const bacnet_ip_t *server);

/* Encodes an error reply (the "unknown property" case). */
int bacnet_encode_error(uint8_t *buffer, uint32_t capacity, uint32_t error);

/* ------------------------------------------------------------------ */
/* The servers                                                          */
/* ------------------------------------------------------------------ */

/* Builds the answer to one BACnet/IP request. `request` is the UDP
 * payload, `response` is where the answer is written. Returns the
 * response length, 0 when the request is ignored, or -1 on a malformed
 * request. */
int bacnet_ip_process(bacnet_ip_t *server, const uint8_t *request,
                       uint32_t request_len, uint8_t *response,
                       uint32_t response_capacity);

/* Builds the answer to one MS/TP frame. The frame carries the 0x55
 * preamble and the length byte; `frame_len` counts them. Returns the
 * number of bytes to send back (the answer without the preamble), 0
 * when the frame is not for us, or -1 on a malformed frame. */
int bacnet_mstp_process(bacnet_mstp_t *server, const uint8_t *frame,
                        uint32_t frame_len, uint8_t *response,
                        uint32_t response_capacity);

/* The MS/TP frame header: preamble, length and the function code. */
#define MSTP_PREAMBLE  0x55
#define MSTP_FUNCTION  0x0E   /* "reply from master" */

/* The MS/TP CRC, as the protocol defines it: the two bytes are
 * added, the result is complemented, and the low byte is sent first. */
uint16_t bacnet_mstp_crc(const uint8_t *data, uint32_t len);

#endif /* BACNET_H */
