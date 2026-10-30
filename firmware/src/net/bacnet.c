/*
 * bacnet.c - the BACnet servers.
 *
 * See bacnet.h for the evidence.
 */

#include <string.h>
#include "bacnet.h"

/* ------------------------------------------------------------------ */
/* The object table                                                    */
/* ------------------------------------------------------------------ */

const bacnet_object_t *bacnet_find_object(const bacnet_object_t *objects,
                                           uint32_t count,
                                           uint32_t instance)
{
    uint32_t i;

    if (objects == 0)
    {
        return 0;
    }
    for (i = 0; i < count; i++)
    {
        if (objects[i].instance == instance)
        {
            return &objects[i];
        }
    }
    return 0;
}

int32_t bacnet_object_value(const bacnet_object_t *obj)
{
    if (obj == 0)
    {
        return 0;
    }
    if (obj->value != 0)
    {
        return *obj->value;
    }
    return obj->literal;
}

int bacnet_object_write(bacnet_object_t *obj, int32_t value)
{
    if (obj == 0 || obj->value == 0 || obj->writable == 0)
    {
        return -1;
    }
    *obj->value = value;
    return 0;
}

/* ------------------------------------------------------------------ */
/* The encoding                                                         */
/* ------------------------------------------------------------------ */

static int put_u8(uint8_t *b, uint32_t cap, uint32_t at, uint8_t v)
{
    if (at >= cap)
    {
        return -1;
    }
    b[at] = v;
    return (int)(at + 1);
}

static int put_u16(uint8_t *b, uint32_t cap, uint32_t at, uint16_t v)
{
    if (at + 2 > cap)
    {
        return -1;
    }
    b[at] = (uint8_t)(v >> 8);
    b[at + 1] = (uint8_t)(v & 0xFF);
    return (int)(at + 2);
}

static int put_u32(uint8_t *b, uint32_t cap, uint32_t at, uint32_t v)
{
    if (at + 4 > cap)
    {
        return -1;
    }
    b[at] = (uint8_t)(v >> 24);
    b[at + 1] = (uint8_t)(v >> 16);
    b[at + 2] = (uint8_t)(v >> 8);
    b[at + 3] = (uint8_t)(v & 0xFF);
    return (int)(at + 4);
}

static int put_string(uint8_t *b, uint32_t cap, uint32_t at, const char *s)
{
    uint32_t n = 0;

    if (s == 0)
    {
        s = "";
    }
    while (s[n] != '\0' && n < 255)
    {
        n++;
    }
    if (at + n + 1 > cap)
    {
        return -1;
    }
    b[at] = (uint8_t)n;
    memcpy(b + at + 1, s, n);
    return (int)(at + n + 1);
}

/* The BACnet application tags. The tag byte carries the tag number in
 * its high nibble; for the unsigned and enumerated tags the low nibble
 * is the length (0x0E for two bytes, 0x0F for four). */
#define TAG_BOOLEAN         0x10
#define TAG_UNSIGNED        0x20
#define TAG_ENUMERATED      0x90
#define TAG_CHARACTERSTRING 0x70

/* The boolean value lives in the tag byte itself. */
static int put_boolean(uint8_t *b, uint32_t cap, uint32_t at, int value)
{
    return put_u8(b, cap, at, (uint8_t)(TAG_BOOLEAN | (value ? 1 : 0)));
}

/* An application tag: the tag number in the high nibble and the length
 * of the value in the low nibble. A length of 5 means the length
 * follows in the next octet. */
static int put_application_tag(uint8_t *b, uint32_t cap, uint32_t at,
                               uint8_t tag, uint32_t value)
{
    uint32_t len;
    int n;

    if (value <= 0xFFu)
    {
        len = 1;
    }
    else if (value <= 0xFFFFu)
    {
        len = 2;
    }
    else if (value <= 0xFFFFFFu)
    {
        len = 3;
    }
    else
    {
        len = 4;
    }

    if (len <= 4)
    {
        n = put_u8(b, cap, at, (uint8_t)(tag | len));
    }
    else
    {
        n = put_u8(b, cap, at, (uint8_t)(tag | 5));
        if (n < 0)
        {
            return n;
        }
        n = put_u8(b, cap, (uint32_t)n, (uint8_t)len);
    }
    if (n < 0)
    {
        return n;
    }
    at = (uint32_t)n;

    if (len == 2)
    {
        return put_u16(b, cap, at, (uint16_t)value);
    }
    if (len == 4)
    {
        return put_u32(b, cap, at, value);
    }
    while (len-- > 0)
    {
        n = put_u8(b, cap, at, (uint8_t)((value >> (8 * len)) & 0xFFu));
        if (n < 0)
        {
            return n;
        }
        at = (uint32_t)n;
    }
    return (int)at;
}

/* Reads an application-tagged unsigned value. Returns the number of
 * bytes consumed, or -1. */
static int get_application_tag(const uint8_t *b, uint32_t len, uint32_t at,
                               uint32_t *value)
{
    uint32_t n;
    uint32_t count;

    if (at >= len)
    {
        return -1;
    }
    if ((b[at] & 0xF0u) != 0x20u && (b[at] & 0xF0u) != 0x90u)
    {
        return -1;
    }
    count = b[at] & 0x0Fu;
    at++;
    if (count == 5u)
    {
        if (at >= len)
        {
            return -1;
        }
        count = b[at];
        at++;
    }
    if (count > 4u || at + count > len)
    {
        return -1;
    }
    n = 0;
    while (count-- > 0)
    {
        n = (n << 8) | b[at];
        at++;
    }
    *value = n;
    return (int)at;
}

/* The object identifier is an unsigned number: the object type in the
 * top ten bits and the instance in the bottom twenty-two. */
static uint32_t object_identifier(uint32_t type, uint32_t instance)
{
    return (type << 22) | (instance & 0x003FFFFFu);
}

static int enc_error(uint8_t *buffer, uint32_t capacity, uint32_t at,
                     uint32_t error)
{
    int n;

    n = put_u8(buffer, capacity, at, 0x10);  /* PDU: error */
    if (n < 0)
    {
        return -1;
    }
    return put_application_tag(buffer, capacity, (uint32_t)n,
                               TAG_ENUMERATED, error);
}

int bacnet_encode_error(uint8_t *buffer, uint32_t capacity, uint32_t error)
{
    return enc_error(buffer, capacity, 0, error);
}

/* The BACnet error codes the servers use. */
#define BAC_ERROR_OTHER          0
#define BAC_ERROR_NO_SUCH_OBJECT 31
#define BAC_ERROR_PROPERTY       32
#define BAC_ERROR_READ_ACCESS    40
#define BAC_ERROR_WRITE_ACCESS   41

static int enc_read_property(uint8_t *buffer, uint32_t capacity, uint32_t at,
                             const bacnet_object_t *object,
                             uint32_t property, int32_t value)
{
    int n;

    if (buffer == 0 || object == 0)
    {
        return -1;
    }

    /* PDU type: complex ACK, with a service choice of 12
     * (ReadProperty). */
    n = put_u8(buffer, capacity, at, 0x30);
    if (n < 0)
    {
        return -1;
    }
    at = (uint32_t)n;
    n = put_u8(buffer, capacity, at, 0x0C);
    if (n < 0)
    {
        return -1;
    }
    at = (uint32_t)n;

    /* The object identifier. */
    n = put_application_tag(buffer, capacity, at, TAG_UNSIGNED,
                            object_identifier(object->object_type,
                                              object->instance));
    if (n < 0)
    {
        return -1;
    }
    at = (uint32_t)n;

    /* The property identifier. */
    n = put_application_tag(buffer, capacity, at, TAG_UNSIGNED, property);
    if (n < 0)
    {
        return -1;
    }
    at = (uint32_t)n;

    switch (property)
    {
    case BAC_PROP_OBJECT_IDENTIFIER:
        n = put_application_tag(buffer, capacity, at, TAG_UNSIGNED,
                                object_identifier(object->object_type,
                                                  object->instance));
        break;
    case BAC_PROP_OBJECT_NAME:
    case BAC_PROP_OBJECT_TYPE:
        if (property == BAC_PROP_OBJECT_TYPE)
        {
            n = put_application_tag(buffer, capacity, at, TAG_ENUMERATED,
                                    object->object_type);
        }
        else
        {
            n = put_string(buffer, capacity, at, object->label ?
                           object->label : object->name);
        }
        break;
    case BAC_PROP_PRESENT_VALUE:
        if (object->object_type == BAC_OBJ_BINARY_VALUE)
        {
            n = put_boolean(buffer, capacity, at, value != 0);
        }
        else
        {
            n = put_application_tag(buffer, capacity, at, TAG_UNSIGNED,
                                    (uint32_t)value);
        }
        break;
    case BAC_PROP_STATUS_FLAGS:
        n = put_boolean(buffer, capacity, at, 0);
        break;
    case BAC_PROP_OUT_OF_SERVICE:
        n = put_boolean(buffer, capacity, at, 0);
        break;
    case BAC_PROP_UNITS:
        n = put_application_tag(buffer, capacity, at, TAG_ENUMERATED,
                                object->units);
        break;
    default:
        return enc_error(buffer, capacity, at, BAC_ERROR_PROPERTY);
    }

    return n;
}

int bacnet_encode_read_property(uint8_t *buffer, uint32_t capacity,
                                const bacnet_object_t *object,
                                uint32_t property, int32_t value)
{
    return enc_read_property(buffer, capacity, 0, object, property, value);
}

int bacnet_encode_device_property(uint8_t *buffer, uint32_t capacity,
                                   uint32_t property,
                                   const bacnet_ip_t *server)
{
    uint32_t at = 0;
    int n;

    if (buffer == 0 || server == 0)
    {
        return -1;
    }

    n = put_u8(buffer, capacity, at, 0x30);     /* complex ACK */
    if (n < 0)
    {
        return -1;
    }
    at = (uint32_t)n;
    n = put_u8(buffer, capacity, at, 0x0C);     /* ReadProperty */
    if (n < 0)
    {
        return -1;
    }
    at = (uint32_t)n;

    n = put_application_tag(buffer, capacity, at, TAG_UNSIGNED,
                            0xFFFFFFFFu);      /* the device object */
    if (n < 0)
    {
        return -1;
    }
    at = (uint32_t)n;

    n = put_application_tag(buffer, capacity, at, TAG_UNSIGNED, property);
    if (n < 0)
    {
        return -1;
    }
    at = (uint32_t)n;

    switch (property)
    {
    case BAC_PROP_OBJECT_IDENTIFIER:
        n = put_application_tag(buffer, capacity, at, TAG_UNSIGNED,
                                object_identifier(BAC_OBJ_ANALOG_VALUE,
                                                  server->device_instance));
        break;
    case 62:      /* vendor-name */
        n = put_string(buffer, capacity, at, server->vendor_name);
        break;
    case 77:      /* model-name */
        n = put_string(buffer, capacity, at, server->model_name);
        break;
    default:
        n = put_string(buffer, capacity, at, server->model_name);
        break;
    }

    return n;
}

/* ------------------------------------------------------------------ */
/* The BACnet/IP server                                                 */
/* ------------------------------------------------------------------ */

/* The BVLC result codes. */
#define BVLC_RESULT       0x00
#define BVLC_WRITE_BROADCAST 0x10

int bacnet_ip_process(bacnet_ip_t *server, const uint8_t *request,
                       uint32_t request_len, uint8_t *response,
                       uint32_t response_capacity)
{
    uint32_t at = 0;
    int n;

    if (server == 0 || request == 0 || response == 0)
    {
        return -1;
    }
    if (request_len < 4)
    {
        return -1;
    }
    /* The BVLC type must be 0x81 ("original unicast NPDU"). */
    if (request[0] != 0x81u)
    {
        return -1;
    }

    n = put_u8(response, response_capacity, at, 0x81u);
    if (n < 0)
    {
        return -1;
    }
    at = (uint32_t)n;
    n = put_u8(response, response_capacity, at, BVLC_RESULT);
    if (n < 0)
    {
        return -1;
    }
    at = (uint32_t)n;
    n = put_u16(response, response_capacity, at, 0);
    if (n < 0)
    {
        return -1;
    }
    at = (uint32_t)n;

    /* The NPDU: version 1, control 0 (expecting a reply). */
    if (request_len < 6)
    {
        return -1;
    }
    if (request[4] != 0x01u)
    {
        return -1;
    }
    n = put_u8(response, response_capacity, at, 0x01u);
    if (n < 0)
    {
        return -1;
    }
    at = (uint32_t)n;
    n = put_u8(response, response_capacity, at, 0x00u);
    if (n < 0)
    {
        return -1;
    }
    at = (uint32_t)n;

    /* The APDU starts at offset 6 for an unconfirmed or simple
     * confirmed request with no addressing. */
    {
        const uint8_t *apdu = request + 6;
        uint32_t apdu_len = request_len - 6;

        if (apdu_len < 4)
        {
            return -1;
        }
        /* The only request the server answers is ReadProperty
         * (confirmed, service 12). */
        if ((apdu[0] & 0xF0u) != 0x00u || apdu[2] != 0x0Cu)
        {
            /* Not understood: answer with an error PDU. */
            return enc_error(response, response_capacity, at,
                             BAC_ERROR_OTHER);
        }

        {
            /* The confirmed request is the PDU type, the invoke id and
             * the service choice, then the parameters. */
            uint32_t p = 3;
            uint32_t object_id = 0;
            uint32_t property = 0;
            const bacnet_object_t *obj;

            /* The object identifier and the property identifier are
             * both application-tagged unsigned values. */
            {
                int used = get_application_tag(apdu, apdu_len, p,
                                                &object_id);
                if (used < 0)
                {
                    return -1;
                }
                p = (uint32_t)used;
                used = get_application_tag(apdu, apdu_len, p, &property);
                if (used < 0)
                {
                    return -1;
                }
                p = (uint32_t)used;
            }

            obj = bacnet_find_object(server->objects, server->object_count,
                                     object_id & 0x003FFFFFu);
            if (obj == 0)
            {
                return enc_error(response, response_capacity, at,
                                 BAC_ERROR_NO_SUCH_OBJECT);
            }
            return enc_read_property(response, response_capacity, at, obj,
                                     property, bacnet_object_value(obj));
        }
    }
}

/* ------------------------------------------------------------------ */
/* The MS/TP server                                                     */
/* ------------------------------------------------------------------ */

uint16_t bacnet_mstp_crc(const uint8_t *data, uint32_t len)
{
    uint32_t i;
    uint8_t crc = 0xFF;

    for (i = 0; i < len; i++)
    {
        uint8_t j;

        crc ^= data[i];
        for (j = 0; j < 8; j++)
        {
            if (crc & 0x01u)
            {
                crc = (uint8_t)(crc >> 1) ^ 0x8Cu;
            }
            else
            {
                crc = (uint8_t)(crc >> 1);
            }
        }
    }
    /* The CRC is complemented, as the standard defines it. */
    return (uint16_t)(~crc);
}

int bacnet_mstp_process(bacnet_mstp_t *server, const uint8_t *frame,
                        uint32_t frame_len, uint8_t *response,
                        uint32_t response_capacity)
{
    uint32_t length;

    if (server == 0 || frame == 0 || response == 0)
    {
        return -1;
    }
    if (frame_len < 4)
    {
        return -1;
    }
    /* The preamble. */
    if (frame[0] != MSTP_PREAMBLE)
    {
        return 0;
    }
    length = frame[1];
    /* The length byte counts the destination address through the end
     * of the data field, so the frame is one byte longer than that. */
    if (length < 2 || (uint64_t)length + 1 > frame_len)
    {
        return -1;
    }
    /* The destination address must be ours. */
    if (frame[2] != server->master_address && frame[2] != 0xFFu)
    {
        return 0;
    }
    /* The source address must not be us. */
    if (frame[3] == server->master_address)
    {
        return 0;
    }

    /* The NPDU and APDU follow the MS/TP header. */
    if (length < 5)
    {
        return 0;
    }
    {
        /* The MS/TP data field is the destination address, the source
         * address and the NPDU with the APDU, so the NPDU starts four
         * bytes into the frame and is two bytes shorter than the data
         * field. */
        const uint8_t *apdu = frame + 4;
        uint32_t apdu_len = length - 2;
        uint32_t p;
        uint32_t object_id = 0;
        uint32_t property = 0;
        const bacnet_object_t *obj;
        int n;

        if (apdu_len < 3)
        {
            return 0;
        }
        /* NPDU: version, control, then the optional destination and
         * source, then the APDU. */
        if (apdu[0] != 0x01u)
        {
            return 0;
        }
        p = 2;
        if (apdu[1] & 0x20u)
        {
            /* The destination network and address. */
            if (p + 3 > apdu_len)
            {
                return 0;
            }
            p += 3 + apdu[p + 2];
        }
        if (apdu[1] & 0x08u)
        {
            /* The source network and address. */
            if (p + 3 > apdu_len)
            {
                return 0;
            }
            p += 3 + apdu[p + 2];
        }
        if (p >= apdu_len)
        {
            return 0;
        }
        if (p >= apdu_len)
        {
            return 0;
        }
        if ((apdu[p] & 0xF0u) != 0x00u || apdu[p + 2] != 0x0Cu)
        {
            return 0;
        }
        p += 3;

        {
            int used = get_application_tag(apdu, apdu_len, p, &object_id);
            if (used < 0)
            {
                return 0;
            }
            p = (uint32_t)used;
            used = get_application_tag(apdu, apdu_len, p, &property);
            if (used < 0)
            {
                return 0;
            }
            p = (uint32_t)used;
        }

        obj = bacnet_find_object(server->objects, server->object_count,
                                 object_id & 0x003FFFFFu);
        if (obj == 0)
        {
            return 0;
        }
        n = bacnet_encode_read_property(response, response_capacity, obj,
                                        property, bacnet_object_value(obj));
        if (n < 0)
        {
            return -1;
        }
        return n;
    }
}
