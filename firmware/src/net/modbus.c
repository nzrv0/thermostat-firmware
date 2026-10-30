/*
 * modbus.c - the Modbus register map and server core.
 */

#include <string.h>
#include "modbus.h"

#define MODBUS_FC_READ_HOLDING   0x03
#define MODBUS_FC_READ_INPUT     0x04
#define MODBUS_FC_WRITE_SINGLE   0x06
#define MODBUS_FC_WRITE_MULTIPLE  0x10

#define MODBUS_EXCEPTION_ILLEGAL_FUNCTION  0x01
#define MODBUS_EXCEPTION_ILLEGAL_ADDRESS  0x02
#define MODBUS_EXCEPTION_ILLEGAL_VALUE    0x03

modbus_point_t g_modbus_points[MODBUS_MAX_POINTS] = {
    { "Actual_Room_temperature", "Room temperature",        MODBUS_POINT_INPUT,    0, 10, 0 },
    { "Humidity",                "Room relative humidity",   MODBUS_POINT_INPUT,    1, 1,  0 },
    { "HeatSetTemp",             "Heat Set Temp",            MODBUS_POINT_HOLDING,  0, 10, 0 },
    { "CoolSetTemp",             "Cool Set Temp",            MODBUS_POINT_HOLDING,  1, 10, 0 },
    { "HoldEndtTime",            "Hold End tTime",           MODBUS_POINT_HOLDING,  2, 1,  0 },
    { "HoldSetTemp",             "Hold Set Temp",            MODBUS_POINT_HOLDING,  3, 10, 0 },
    { "HolidayStarttTime",       "Holiday Start tTime",      MODBUS_POINT_HOLDING,  4, 1,  0 },
    { "HolidayEndtTime",         "Holiday End tTime",        MODBUS_POINT_HOLDING,  5, 1,  0 },
    { "MaxSetTempLimit",         "Max Set Temp Limit",       MODBUS_POINT_HOLDING,  6, 10, 0 },
    { "MinSetTempLimit",         "Min Set Temp Limit",       MODBUS_POINT_HOLDING,  7, 10, 0 },
    { "W1relaystatus",           "Heating relay W1 status",  MODBUS_POINT_INPUT,    2, 1,  0 },
    { "W2relaystatus",           "Heating relay W2 status",  MODBUS_POINT_INPUT,    3, 1,  0 },
    { "Y1relaystatus",           "Cooling relay Y1 status",  MODBUS_POINT_INPUT,    4, 1,  0 },
    { "Y2relaystatus",           "Cooling relay Y2 status",  MODBUS_POINT_INPUT,    5, 1,  0 },
    { "Fanrelaystatus",          "Fan relay G status",       MODBUS_POINT_INPUT,    6, 1,  0 },
    { "Mode",                    "Mode",                     MODBUS_POINT_INPUT,    7, 1,  0 },
    { "HeatingStatus",           "Heating Status",           MODBUS_POINT_INPUT,    8, 1,  0 },
    { "FlagiSTouchPadLock",      "Flag TouchPad Lock",       MODBUS_POINT_INPUT,    9, 1,  0 },
    { "FlagiSHolidaying",        "Flag Holidaying",          MODBUS_POINT_INPUT,   10, 1,  0 },
    { "FlagiSHolding",           "Flag Holding",             MODBUS_POINT_INPUT,   11, 1,  0 }
};

const uint16_t g_modbus_point_count =
    (uint16_t)(sizeof(g_modbus_points) / sizeof(g_modbus_points[0]));

const modbus_point_t *modbus_find(uint16_t address, modbus_point_kind_t kind)
{
    uint16_t i;

    for (i = 0; i < g_modbus_point_count; i++)
    {
        if (g_modbus_points[i].address == address && g_modbus_points[i].kind == kind)
        {
            return &g_modbus_points[i];
        }
    }
    return 0;
}

uint16_t modbus_crc16(const uint8_t *data, int length)
{
    uint16_t crc = 0xFFFF;
    int i;
    int b;

    for (i = 0; i < length; i++)
    {
        crc ^= (uint16_t)data[i];
        for (b = 0; b < 8; b++)
        {
            if (crc & 1)
            {
                crc = (uint16_t)((crc >> 1) ^ 0xA001);
            }
            else
            {
                crc = (uint16_t)(crc >> 1);
            }
        }
    }
    return crc;
}

static uint16_t point_value(const modbus_point_t *point)
{
    if (point->scale > 1)
    {
        return (uint16_t)(point->raw / point->scale);
    }
    return point->raw;
}

static int pdu_read(uint8_t *pdu, int pdu_length, modbus_point_kind_t kind)
{
    uint16_t start;
    uint16_t count;
    uint16_t i;
    int offset = 2;

    if (pdu_length < 5)
    {
        return -1;
    }
    offset = 1;
    start = (uint16_t)((pdu[offset] << 8) | pdu[offset + 1]);
    count = (uint16_t)((pdu[offset + 2] << 8) | pdu[offset + 3]);
    offset += 4;
    (void)offset;

    if (count == 0 || count > 125)
    {
        return -1;
    }

    pdu[1] = (uint8_t)(count * 2);

    for (i = 0; i < count; i++)
    {
        const modbus_point_t *point = modbus_find((uint16_t)(start + i), kind);
        uint16_t value = (point != 0) ? point_value(point) : 0;

        pdu[2 + i * 2] = (uint8_t)(value >> 8);
        pdu[3 + i * 2] = (uint8_t)(value & 0xFF);
    }
    return 2 + count * 2;
}

static int pdu_write_single(uint8_t *pdu, int pdu_length, modbus_point_kind_t kind)
{
    uint16_t address;
    uint16_t value;
    modbus_point_t *point;
    uint16_t i;

    if (pdu_length < 5)
    {
        return -1;
    }
    address = (uint16_t)((pdu[1] << 8) | pdu[2]);
    value = (uint16_t)((pdu[3] << 8) | pdu[4]);

    for (i = 0; i < g_modbus_point_count; i++)
    {
        if (g_modbus_points[i].address == address &&
            g_modbus_points[i].kind == kind)
        {
            point = (modbus_point_t *)&g_modbus_points[i];
            point->raw = (point->scale > 1) ? (uint16_t)(value * point->scale) : value;
            pdu[3] = (uint8_t)(value >> 8);
            pdu[4] = (uint8_t)(value & 0xFF);
            return 5;
        }
    }
    return -1;
}

static int pdu_process(uint8_t *pdu, int pdu_length)
{
    uint8_t function;

    if (pdu_length < 1)
    {
        return -1;
    }

    function = pdu[0];

    switch (function)
    {
    case MODBUS_FC_READ_HOLDING:
        return pdu_read(pdu, pdu_length, MODBUS_POINT_HOLDING);
    case MODBUS_FC_READ_INPUT:
        return pdu_read(pdu, pdu_length, MODBUS_POINT_INPUT);
    case MODBUS_FC_WRITE_SINGLE:
        return pdu_write_single(pdu, pdu_length, MODBUS_POINT_HOLDING);
    case MODBUS_FC_WRITE_MULTIPLE:
    {
        uint16_t address;
        uint16_t count;
        uint16_t i;
        int offset = 6;

        if (pdu_length < 7)
        {
            return -1;
        }
        address = (uint16_t)((pdu[1] << 8) | pdu[2]);
        count = (uint16_t)((pdu[3] << 8) | pdu[4]);
        if (count == 0 || count > 123 || pdu_length < 7 + count * 2)
        {
            return -1;
        }
        for (i = 0; i < count; i++)
        {
            uint16_t value = (uint16_t)((pdu[offset + i * 2] << 8) |
                                        pdu[offset + i * 2 + 1]);
            uint16_t j;

            for (j = 0; j < g_modbus_point_count; j++)
            {
                if (g_modbus_points[j].address == (uint16_t)(address + i) &&
                    g_modbus_points[j].kind == MODBUS_POINT_HOLDING)
                {
                    modbus_point_t *point = &g_modbus_points[j];

                    point->raw = (point->scale > 1)
                        ? (uint16_t)(value * point->scale) : value;
                }
            }
        }
        return 5;
    }
    default:
        pdu[0] = (uint8_t)(function | 0x80);
        pdu[1] = MODBUS_EXCEPTION_ILLEGAL_FUNCTION;
        return 2;
    }
}

static int pdu_exception(uint8_t *pdu, uint8_t code)
{
    pdu[0] = (uint8_t)(pdu[0] | 0x80);
    pdu[1] = code;
    return 2;
}

int modbus_tcp_process(const uint8_t *request, int request_length,
                        uint8_t *response, int response_capacity)
{
    uint8_t pdu[MODBUS_PDU_MAX];
    int pdu_length;
    int response_length;

    if (request == 0 || response == 0)
    {
        return 0;
    }
    if (request_length < MODBUS_TCP_HEADER + 2 ||
        response_capacity < MODBUS_TCP_HEADER + 3)
    {
        return 0;
    }

    memcpy(response, request, MODBUS_TCP_HEADER);
    response[4] = 0;
    response[5] = 0;

    pdu_length = request_length - MODBUS_TCP_HEADER;
    if (pdu_length > MODBUS_PDU_MAX)
    {
        return 0;
    }
    memcpy(pdu, request + MODBUS_TCP_HEADER, (size_t)pdu_length);

    pdu_length = pdu_process(pdu, pdu_length);
    if (pdu_length < 0)
    {
        pdu[0] = request[MODBUS_TCP_HEADER];
        pdu_length = pdu_exception(pdu, MODBUS_EXCEPTION_ILLEGAL_ADDRESS);
    }

    if (response_capacity < MODBUS_TCP_HEADER + pdu_length)
    {
        return 0;
    }
    response[6] = (uint8_t)pdu_length;
    memcpy(response + MODBUS_TCP_HEADER, pdu, (size_t)pdu_length);
    response_length = MODBUS_TCP_HEADER + pdu_length;

    response[4] = (uint8_t)((response_length - 6) >> 8);
    response[5] = (uint8_t)(response_length - 6);
    return response_length;
}

int modbus_rtu_process(const uint8_t *request, int request_length,
                        uint8_t *response, int response_capacity)
{
    uint8_t pdu[MODBUS_PDU_MAX];
    int pdu_length;
    uint16_t crc;

    if (request == 0 || response == 0 || request_length < 4 ||
        response_capacity < 8)
    {
        return 0;
    }

    pdu_length = request_length - 3;
    if (pdu_length > MODBUS_PDU_MAX)
    {
        return 0;
    }
    memcpy(pdu, request + 1, (size_t)pdu_length);

    pdu_length = pdu_process(pdu, pdu_length);
    if (pdu_length < 0)
    {
        pdu[0] = request[1];
        pdu_length = pdu_exception(pdu, MODBUS_EXCEPTION_ILLEGAL_ADDRESS);
    }

    if (response_capacity < pdu_length + 4)
    {
        return 0;
    }
    response[0] = request[0];
    memcpy(response + 1, pdu, (size_t)pdu_length);
    crc = modbus_crc16(response, pdu_length + 1);
    response[pdu_length + 1] = (uint8_t)(crc & 0xFF);
    response[pdu_length + 2] = (uint8_t)(crc >> 8);
    return pdu_length + 3;
}
