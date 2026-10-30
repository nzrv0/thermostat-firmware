/*
 * mqtt_sn.h - MQTT-SN packet encoding.
 *
 * The firmware speaks MQTT-SN to the ESP-07S; the magic string
 * "MQIsdp" is at 0x08045680. This module builds the packets the
 * application sends.
 */

#ifndef MQTT_SN_H
#define MQTT_SN_H

#include <stddef.h>
#include <stdint.h>

#define MQTT_SN_LENGTH_MASK   0xFF
#define MQTT_SN_LENGTH_2      0x01
#define MQTT_SN_LENGTH_4      0x02

typedef enum
{
    MQTT_SN_CONNECT = 0x04,
    MQTT_SN_CONNACK = 0x05,
    MQTT_SN_PUBLISH = 0x06,
    MQTT_SN_PUBACK = 0x07,
    MQTT_SN_SUBSCRIBE = 0x08,
    MQTT_SN_SUBACK = 0x09,
    MQTT_SN_UNSUBSCRIBE = 0x0A,
    MQTT_SN_PINGREQ = 0x0C,
    MQTT_SN_PINGRESP = 0x0D,
    MQTT_SN_DISCONNECT = 0x0E
} mqtt_sn_type_t;

typedef struct
{
    uint8_t  flags;
    uint8_t  protocol_id;
    uint16_t duration;      /* keep alive, seconds */
    uint8_t  client_id[16];
} mqtt_sn_connect_t;

typedef struct
{
    uint8_t  flags;
    uint8_t  topic_id;
    uint16_t message_id;
    const char *topic;      /* short topic name, NULL for an id */
    const uint8_t *payload;
    uint16_t payload_length;
} mqtt_sn_publish_t;

/* Encodes a CONNECT packet. Returns the packet length. */
int mqtt_sn_encode_connect(uint8_t *out, int capacity,
                           const mqtt_sn_connect_t *connect);

/* Encodes a PUBLISH packet. Returns the packet length. */
int mqtt_sn_encode_publish(uint8_t *out, int capacity,
                           const mqtt_sn_publish_t *publish);

/* Encodes a SUBSCRIBE packet. Returns the packet length. */
int mqtt_sn_encode_subscribe(uint8_t *out, int capacity, uint8_t flags,
                             uint16_t message_id, const char *topic);

/* The message type of a received packet, or 0 when it is not one. */
uint8_t mqtt_sn_message_type(const uint8_t *packet, int length);

#endif /* MQTT_SN_H */
