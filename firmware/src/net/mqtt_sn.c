/*
 * mqtt_sn.c - MQTT-SN packet encoding.
 */

#include <string.h>
#include "mqtt_sn.h"

static int put_u16(uint8_t *out, uint16_t value)
{
    out[0] = (uint8_t)(value >> 8);
    out[1] = (uint8_t)(value & 0xFF);
    return 2;
}

int mqtt_sn_encode_connect(uint8_t *out, int capacity,
                           const mqtt_sn_connect_t *connect)
{
    int length = 0;
    int id_length;

    if (out == 0 || connect == 0 || capacity < 16)
    {
        return 0;
    }

    id_length = (int)strnlen((const char *)connect->client_id,
                              sizeof(connect->client_id));

    out[length++] = MQTT_SN_CONNECT;
    out[length++] = (uint8_t)(2 + id_length);   /* length */
    out[length++] = connect->flags;
    out[length++] = connect->protocol_id;
    length += put_u16(out + length, connect->duration);
    memcpy(out + length, connect->client_id, (size_t)id_length);
    length += id_length;
    return length;
}

int mqtt_sn_encode_publish(uint8_t *out, int capacity,
                           const mqtt_sn_publish_t *publish)
{
    int length = 0;
    int topic_length;

    if (out == 0 || publish == 0)
    {
        return 0;
    }

    topic_length = (publish->topic != 0)
        ? (int)strlen(publish->topic) : 0;

    if (capacity < 7 + topic_length + publish->payload_length)
    {
        return 0;
    }

    out[length++] = MQTT_SN_PUBLISH;
    if (topic_length == 0)
    {
        out[length++] = (uint8_t)(2 + publish->payload_length);
    }
    else
    {
        out[length++] = (uint8_t)(3 + topic_length + publish->payload_length);
    }
    out[length++] = publish->flags;
    out[length++] = publish->topic_id;
    length += put_u16(out + length, publish->message_id);
    if (topic_length > 0)
    {
        memcpy(out + length, publish->topic, (size_t)topic_length);
        length += topic_length;
    }
    if (publish->payload_length > 0 && publish->payload != 0)
    {
        memcpy(out + length, publish->payload, publish->payload_length);
        length += publish->payload_length;
    }
    return length;
}

int mqtt_sn_encode_subscribe(uint8_t *out, int capacity, uint8_t flags,
                             uint16_t message_id, const char *topic)
{
    int length = 0;
    int topic_length;

    if (out == 0 || topic == 0)
    {
        return 0;
    }

    topic_length = (int)strlen(topic);
    if (capacity < 6 + topic_length)
    {
        return 0;
    }

    out[length++] = MQTT_SN_SUBSCRIBE;
    out[length++] = (uint8_t)(3 + topic_length);
    out[length++] = flags;
    length += put_u16(out + length, message_id);
    memcpy(out + length, topic, (size_t)topic_length);
    length += topic_length;
    return length;
}

uint8_t mqtt_sn_message_type(const uint8_t *packet, int length)
{
    if (packet == 0 || length < 2)
    {
        return 0;
    }
    return packet[0];
}
