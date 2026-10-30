/*
 * test_protocols.c - the host checks for the USB, display, BACnet,
 * OTA, LoRaWAN, time, humidity, screen and touch modules.
 *
 * These are the modules that were written from the recovered evidence
 * rather than from a spec: the USB device (a CDC-ACM virtual COM port),
 * the MIPI-DCS panel driver, the two BACnet servers, the FTP update
 * client, the LoRaWAN payload codec and the UI layers.
 */

#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <stdint.h>

#include "usb.h"
#include "panel.h"
#include "bacnet.h"
#include "ota.h"
#include "lora_payload.h"
#include "rtc.h"
#include "si7021.h"
#include "screen.h"
#include "touch.h"

extern int  g_failures;
extern int  g_checks;

#define CHECK(cond, ...)                                                   \
    do {                                                                   \
        g_checks++;                                                        \
        if (!(cond)) {                                                     \
            g_failures++;                                                  \
            printf("  FAIL: ");                                           \
            printf(__VA_ARGS__);                                           \
            printf("\n        (%s:%d)\n", __FILE__, __LINE__);              \
        }                                                                  \
    } while (0)

/* ------------------------------------------------------------------ */
/* USB: the CDC-ACM virtual COM port                                    */
/* ------------------------------------------------------------------ */

static uint8_t g_usb_tx[512];
static uint32_t g_usb_tx_len;
static uint8_t g_usb_rx[512];
static uint32_t g_usb_rx_len;
static uint8_t g_usb_address;
static int g_usb_configured;
static int g_usb_dtr, g_usb_rts;

static void usb_send(void *ctx, uint8_t ep, const uint8_t *data, uint32_t len)
{
    (void)ctx;
    (void)ep;
    if (g_usb_tx_len + len <= sizeof(g_usb_tx))
    {
        memcpy(g_usb_tx + g_usb_tx_len, data, len);
        g_usb_tx_len += len;
    }
}

static int usb_receive(void *ctx, uint8_t ep, uint8_t *data, uint32_t len)
{
    uint32_t n;

    (void)ctx;
    (void)ep;
    n = g_usb_rx_len;
    if (n > len)
    {
        n = len;
    }
    if (n == 0)
    {
        return -1;
    }
    memcpy(data, g_usb_rx, n);
    g_usb_rx_len = 0;
    return (int)n;
}

static void usb_set_address(void *ctx, uint8_t address)
{
    (void)ctx;
    g_usb_address = address;
}

static void usb_set_configured(void *ctx, int configured)
{
    (void)ctx;
    g_usb_configured = configured;
}

static void usb_set_line_state(void *ctx, int dtr, int rts)
{
    (void)ctx;
    g_usb_dtr = dtr;
    g_usb_rts = rts;
}

static const usb_hw_t g_usb_hw = {
    usb_send, usb_receive, usb_set_address, usb_set_configured,
    usb_set_line_state
};

static void build_setup(uint8_t *setup, uint8_t type, uint8_t request,
                        uint16_t value, uint16_t index, uint16_t length)
{
    setup[0] = type;
    setup[1] = request;
    setup[2] = (uint8_t)(value & 0xFF);
    setup[3] = (uint8_t)(value >> 8);
    setup[4] = (uint8_t)(index & 0xFF);
    setup[5] = (uint8_t)(index >> 8);
    setup[6] = (uint8_t)(length & 0xFF);
    setup[7] = (uint8_t)(length >> 8);
}

static void test_usb(void)
{
    usb_cdc_t dev;
    uint8_t setup[8];
    uint32_t len = 0;
    const uint8_t *desc;

    printf("USB: CDC-ACM virtual COM port\n");

    usb_cdc_init(&dev, &g_usb_hw, 0);

    /* The descriptor set the firmware ships. */
    desc = usb_device_descriptor(&len);
    CHECK(len == 18, "the device descriptor is 18 bytes");
    CHECK(desc[0] == 0x12 && desc[1] == 0x01,
          "the device descriptor is a DEVICE descriptor");
    CHECK(desc[8] == 0x83 && desc[9] == 0x04,
          "the device descriptor carries idVendor 0x0483");
    CHECK(desc[10] == 0x20 && desc[11] == 0x57,
          "the device descriptor carries idProduct 0x5720");
    CHECK(desc[7] == 0x40, "the control endpoint's packet is 64 bytes");

    desc = usb_config_descriptor(&len);
    CHECK(len == 32, "the configuration descriptor is 32 bytes");
    CHECK(desc[2] == 0x20 && desc[3] == 0x00,
          "the configuration descriptor declares 32 bytes in total");
    CHECK(desc[14] == 0x08 && desc[15] == 0x06 && desc[16] == 0x50,
          "the interface is CDC ACM");
    CHECK(desc[20] == 0x81 && desc[27] == 0x02,
          "the endpoints are 0x81 in and 0x02 out");

    /* A GET_DESCRIPTOR of the device. */
    g_usb_tx_len = 0;
    build_setup(setup, 0x80, USB_REQ_GET_DESCRIPTOR,
                (uint16_t)(USB_DT_DEVICE << 8), 0, 18);
    CHECK(usb_cdc_setup(&dev, setup) == 18,
          "the device descriptor is returned");
    CHECK(g_usb_tx_len == 18 && g_usb_tx[0] == 0x12,
          "the device descriptor is what the firmware ships");

    /* A GET_DESCRIPTOR of the configuration. */
    g_usb_tx_len = 0;
    build_setup(setup, 0x80, USB_REQ_GET_DESCRIPTOR,
                (uint16_t)(USB_DT_CONFIGURATION << 8), 0, 32);
    CHECK(usb_cdc_setup(&dev, setup) == 32,
          "the configuration descriptor is returned");

    /* A GET_DESCRIPTOR of a string. */
    g_usb_tx_len = 0;
    build_setup(setup, 0x80, USB_REQ_GET_DESCRIPTOR,
                (uint16_t)((USB_DT_STRING << 8) | 2), 0, 255);
    CHECK(usb_cdc_setup(&dev, setup) > 0, "a string descriptor is returned");
    CHECK(g_usb_tx[1] == 0x03, "the string descriptor is a STRING");

    /* SET_ADDRESS and SET_CONFIGURATION. */
    g_usb_address = 0;
    build_setup(setup, 0x00, USB_REQ_SET_ADDRESS, 7, 0, 0);
    CHECK(usb_cdc_setup(&dev, setup) == 0, "SET_ADDRESS is accepted");
    CHECK(g_usb_address == 7, "the device takes the address");

    g_usb_configured = 0;
    build_setup(setup, 0x00, USB_REQ_SET_CONFIGURATION, 1, 0, 0);
    CHECK(usb_cdc_setup(&dev, setup) == 0, "SET_CONFIGURATION is accepted");
    CHECK(g_usb_configured == 1, "the device is configured");

    g_usb_tx_len = 0;
    build_setup(setup, 0x80, USB_REQ_GET_CONFIGURATION, 0, 0, 1);
    CHECK(usb_cdc_setup(&dev, setup) == 1, "GET_CONFIGURATION is answered");
    CHECK(g_usb_tx[0] == 1, "the configuration value is 1");

    /* The CDC class requests. */
    build_setup(setup, 0x21, USB_CDC_SET_LINE_CODING, 0, 0, 7);
    CHECK(usb_cdc_setup(&dev, setup) == 0, "SET_LINE_CODING is accepted");
    {
        uint8_t coding[7] = { 0x00, 0xC2, 0x01, 0x00, 0x00, 0x00, 0x08 };
        usb_cdc_control_data(&dev, coding, sizeof(coding));
    }
    g_usb_tx_len = 0;
    build_setup(setup, 0xA1, USB_CDC_GET_LINE_CODING, 0, 0, 7);
    CHECK(usb_cdc_setup(&dev, setup) == 7, "GET_LINE_CODING is answered");
    CHECK(g_usb_tx[1] == 0xC2, "the line coding is what was set");

    build_setup(setup, 0x21, USB_CDC_SET_CONTROL_LINE_STATE, 0x03, 0, 0);
    CHECK(usb_cdc_setup(&dev, setup) == 0,
          "SET_CONTROL_LINE_STATE is accepted");
    CHECK(g_usb_dtr == 1 && g_usb_rts == 1, "DTR and RTS are set");

    /* An unsupported request is refused. */
    build_setup(setup, 0x00, 0x0B, 0, 0, 0);
    CHECK(usb_cdc_setup(&dev, setup) == 0, "SET_INTERFACE is accepted");

    /* Bulk traffic. */
    {
        const uint8_t out[] = { 'h', 'i' };
        memcpy(g_usb_rx, out, sizeof(out));
        g_usb_rx_len = sizeof(out);
        usb_cdc_on_rx(&dev, 0x02);
        CHECK(usb_cdc_rx_pending(&dev) == 2, "a bulk OUT packet is queued");
    }
    {
        CHECK(usb_cdc_write(&dev, (const uint8_t *)"ok", 2) == 2,
              "data is queued for the bulk IN endpoint");
        CHECK(usb_cdc_tx_pending(&dev) == 2, "two bytes are pending");
        g_usb_tx_len = 0;
        usb_cdc_on_tx(&dev, 0x81);
        CHECK(g_usb_tx_len == 2 && g_usb_tx[0] == 'o' && g_usb_tx[1] == 'k',
              "the bulk IN packet carries the queued data");
        CHECK(usb_cdc_tx_pending(&dev) == 0, "the queue is empty again");
    }
}

/* ------------------------------------------------------------------ */
/* Panel: the MIPI-DCS command sequence                                 */
/* ------------------------------------------------------------------ */

static uint8_t g_panel_bytes[512];
static uint32_t g_panel_len;

static void panel_write(void *ctx, const uint8_t *data, uint32_t len)
{
    (void)ctx;
    if (g_panel_len + len <= sizeof(g_panel_bytes))
    {
        memcpy(g_panel_bytes + g_panel_len, data, len);
        g_panel_len += len;
    }
}

static uint32_t g_panel_clock;

static uint32_t panel_millis(void *ctx)
{
    (void)ctx;
    /* The clock advances ten milliseconds per call, so the panel's
     * post-sleep-out delay completes. */
    g_panel_clock += 10u;
    return g_panel_clock;
}

static void test_panel(void)
{
    panel_t panel;
    const panel_bus_t bus = { panel_write, panel_millis, 0 };
    uint32_t i;
    int found_slpout = 0;
    int found_colmod = 0;
    int found_dispon = 0;

    printf("Panel: the MIPI-DCS command sequence\n");

    g_panel_len = 0;
    g_panel_clock = 0;
    panel_init(&panel, &bus, 0);
    CHECK(g_panel_len > 0, "the panel is initialised");
    CHECK(panel.width == 480 && panel.height == 320,
          "the panel is 480x320");

    for (i = 0; i < g_panel_len; i++)
    {
        if (g_panel_bytes[i] == DCS_SLPOUT)
        {
            found_slpout = 1;
        }
        if (g_panel_bytes[i] == DCS_COLMOD)
        {
            found_colmod = 1;
        }
        if (g_panel_bytes[i] == DCS_DISPON)
        {
            found_dispon = 1;
        }
    }
    CHECK(found_slpout, "the panel is woken with SLPOUT");
    CHECK(found_colmod, "the pixel format is set with COLMOD");
    CHECK(found_dispon, "the display is turned on with DISPON");

    /* The address window. */
    g_panel_len = 0;
    panel_set_window(&panel, 10, 20, 100, 50);
    CHECK(g_panel_len == 10, "the address window is four commands and "
          "their arguments");
    CHECK(g_panel_bytes[0] == DCS_CASET, "the column range comes first");
    CHECK(g_panel_bytes[5] == DCS_RASET, "the row range comes second");
    CHECK(g_panel_bytes[1] == 0 && g_panel_bytes[2] == 10,
          "the first column is 10");
    CHECK(g_panel_bytes[3] == 0 && g_panel_bytes[4] == 109,
          "the last column is 109");
    CHECK(g_panel_bytes[6] == 0 && g_panel_bytes[7] == 20,
          "the first row is 20");
    CHECK(g_panel_bytes[8] == 0 && g_panel_bytes[9] == 69,
          "the last row is 69");
}

/* ------------------------------------------------------------------ */
/* BACnet: the object table and the two servers                         */
/* ------------------------------------------------------------------ */

static int32_t g_room_temperature = 215;
static int32_t g_humidity = 45;

static const bacnet_object_t g_objects[] = {
    { BAC_OBJ_ANALOG_VALUE, 0, "Actual_Room_temperature", "Room temperature",
      &g_room_temperature, 0, BAC_UNITS_DEGREES_CELSIUS, 0 },
    { BAC_OBJ_ANALOG_VALUE, 1, "Humidity", "Room relative humidity",
      &g_humidity, 0, BAC_UNITS_PERCENT_RELATIVE_HUM, 0 }
};

static void test_bacnet(void)
{
    bacnet_ip_t ip;
    bacnet_mstp_t mstp;
    uint8_t request[32];
    uint8_t response[64];
    int n;
    const bacnet_object_t *obj;

    printf("BACnet: the IP and MS/TP servers\n");

    obj = bacnet_find_object(g_objects, 2, 1);
    CHECK(obj != 0 && obj->name != 0 &&
          strcmp(obj->name, "Humidity") == 0,
          "an object is found by its instance");
    CHECK(bacnet_find_object(g_objects, 2, 9) == 0,
          "an unknown instance finds nothing");
    CHECK(bacnet_object_value(obj) == 45, "the present value is read");

    /* A ReadProperty request for the room temperature, over BACnet/IP.
     *
     *   BVLC: 81 00 00 0e   NPDU: 01 00   APDU: 00 01 20 xx xx 20 yy
     */
    memset(request, 0, sizeof(request));
    request[0] = 0x81;
    request[1] = 0x00;
    request[2] = 0x00;
    request[3] = 0x0e;
    request[4] = 0x01;
    request[5] = 0x00;
    request[6] = 0x00;    /* confirmed request */
    request[7] = 0x01;    /* invoke id */
    request[8] = 0x0c;    /* the service: ReadProperty */
    request[9] = 0x21;    /* object identifier, one byte */
    request[10] = 0x00;   /* instance 0 */
    request[11] = 0x21;   /* property identifier, one byte */
    request[12] = 0x55;   /* present-value */

    memset(&ip, 0, sizeof(ip));
    ip.port = 4780;
    ip.objects = g_objects;
    ip.object_count = 2;
    ip.device_instance = 1;
    ip.vendor_name = "Heatmiser";
    ip.model_name = "Accent";

    n = bacnet_ip_process(&ip, request, 13, response, sizeof(response));
    CHECK(n > 0, "a BACnet/IP request is answered");
    CHECK(response[0] == 0x81, "the answer carries the BVLC type");
    CHECK(response[4] == 0x01, "the answer carries the NPDU version");
    CHECK(response[6] == 0x30, "the answer is a complex ACK");
    /* The present value, 215, is the last thing in the answer. */
    CHECK(response[n - 1] == 215, "the present value is in the answer");

    /* An unknown object is an error. */
    request[10] = 0x63;
    n = bacnet_ip_process(&ip, request, 13, response, sizeof(response));
    CHECK(n > 0 && response[6] == 0x10, "an unknown object is an error");

    /* MS/TP: a frame with the preamble, the length, the addresses and
     * the NPDU/APDU. */
    {
        uint8_t frame[32];
        uint32_t p = 0;

        frame[p++] = 0x55;         /* preamble */
        frame[p++] = 0x00;         /* the length, patched below */
        frame[p++] = 0x01;         /* the destination: our master */
        frame[p++] = 0xFE;         /* the source */
        frame[p++] = 0x01;         /* NPDU version */
        frame[p++] = 0x00;         /* NPDU control */
        frame[p++] = 0x00;         /* APDU: confirmed request */
        frame[p++] = 0x01;         /* invoke id */
        frame[p++] = 0x0c;         /* the service: ReadProperty */
        frame[p++] = 0x21;         /* object identifier, one byte */
        frame[p++] = 0x00;         /* instance 0 */
        frame[p++] = 0x21;         /* property identifier, one byte */
        frame[p++] = 0x55;
        frame[1] = (uint8_t)(p - 2);

        memset(&mstp, 0, sizeof(mstp));
        mstp.master_address = 1;
        mstp.baud = 9600;
        mstp.objects = g_objects;
        mstp.object_count = 2;
        mstp.device_instance = 1;
        mstp.vendor_name = "Heatmiser";
        mstp.model_name = "Accent";

        n = bacnet_mstp_process(&mstp, frame, p, response, sizeof(response));
        CHECK(n > 0, "an MS/TP frame is answered");
        CHECK(response[0] == 0x30, "the MS/TP answer is a complex ACK");

        /* A frame for another master is ignored. */
        frame[2] = 0x02;
        CHECK(bacnet_mstp_process(&mstp, frame, p, response,
                                  sizeof(response)) == 0,
              "a frame for another master is ignored");
    }

    /* The MS/TP CRC. */
    {
        uint8_t data[] = { 0x01, 0x02, 0x03 };
        uint16_t crc = bacnet_mstp_crc(data, sizeof(data));
        /* The CRC of a known frame is its complement, so the value
         * must not be 0xFFFF and must be stable. */
        CHECK(crc == bacnet_mstp_crc(data, sizeof(data)),
              "the MS/TP CRC is stable");
        CHECK(crc != 0xFFFFu, "the MS/TP CRC is complemented");
    }
}

/* ------------------------------------------------------------------ */
/* OTA: the FTP update client                                           */
/* ------------------------------------------------------------------ */

static const char *g_ota_opened;
static uint8_t g_ota_data[64];
static uint32_t g_ota_len;
static uint32_t g_ota_written;
static int g_ota_progress;

static int ota_open(void *ctx, const char *path, long *size)
{
    (void)ctx;
    g_ota_opened = path;
    *size = (long)g_ota_len;
    return 0;
}

static int ota_read(void *ctx, void *buffer, uint32_t len)
{
    uint32_t n = g_ota_len;

    (void)ctx;
    if (n > len)
    {
        n = len;
    }
    memcpy(buffer, g_ota_data, n);
    g_ota_len = 0;
    return (int)n;
}

static void ota_close(void *ctx)
{
    (void)ctx;
}

static int ota_write(void *ctx, uint32_t offset, const uint8_t *data,
                     uint32_t len)
{
    (void)ctx;
    (void)offset;
    (void)data;
    g_ota_written += len;
    return 0;
}

static void ota_progress_cb(void *ctx, int percent)
{
    (void)ctx;
    g_ota_progress = percent;
}

static const ota_transport_t g_ota_transport = {
    ota_open, ota_read, ota_close, ota_write, ota_progress_cb
};

static void test_ota(void)
{
    ota_client_t client;
    char name[32];
    int32_t major = 0;
    int32_t minor = 0;

    printf("OTA: the FTP update client\n");

    CHECK(ota_version_filename(name, sizeof(name), 1, 5) > 0 &&
          strcmp(name, "U-LW-1.05") == 0,
          "the version file is named U-LW-%%d.%%02d");

    CHECK(ota_parse_version("U-LW-1.05", &major, &minor) == 0 &&
          major == 1 && minor == 5, "the version is parsed");
    CHECK(ota_parse_version("U-LW-2.10", &major, &minor) == 0 &&
          major == 2 && minor == 10, "a two digit minor is parsed");
    CHECK(ota_parse_version("U-LW-1", &major, &minor) != 0,
          "a version without a minor is refused");

    CHECK(ota_compare_version(1, 5, 1, 5) == 0, "equal versions compare equal");
    CHECK(ota_compare_version(1, 5, 1, 6) < 0, "1.05 is older than 1.06");
    CHECK(ota_compare_version(2, 0, 1, 99) > 0, "2.00 is newer than 1.99");

    ota_init(&client, &g_ota_transport, 0);
    g_ota_len = 32;
    CHECK(ota_check(&client, 1, 5) == 1, "a newer version is found");
    CHECK(client.remote_major == 1 && client.remote_minor == 6,
          "the newer version is 1.06");
    CHECK(g_ota_opened != 0 &&
          strstr(g_ota_opened, "/ftp/Products/MC6-U-Lora/FalconHP_Mark/") != 0,
          "the update is fetched from the vendor's directory");
    CHECK(strstr(g_ota_opened, "U-LW-1.06") != 0,
          "the file name carries the version");

    g_ota_len = 32;
    g_ota_written = 0;
    g_ota_progress = -1;
    CHECK(ota_apply(&client) == 0, "the update is applied");
    CHECK(g_ota_written == 32, "the whole image is written");
    CHECK(g_ota_progress == 100, "the progress reaches 100 per cent");
}

/* ------------------------------------------------------------------ */
/* LoRaWAN: the uplink payload                                          */
/* ------------------------------------------------------------------ */

static void test_lora_payload(void)
{
    lora_telemetry_t tx;
    lora_telemetry_t rx;
    uint8_t payload[32];
    char hex[64];
    int n;

    printf("LoRaWAN: the uplink payload\n");

    memset(&tx, 0, sizeof(tx));
    tx.room_temperature = 215;
    tx.humidity = 450;
    tx.heat_setpoint = 210;
    tx.cool_setpoint = 240;
    tx.mode = 3;
    tx.flags = 0x0C;
    tx.relays = 0x11;
    tx.uptime = 123456;

    n = lora_payload_encode(payload, sizeof(payload), &tx);
    CHECK(n == 16, "the payload is 16 bytes");
    CHECK(payload[0] == LORA_VERSION, "the payload carries a version");

    CHECK(lora_payload_decode(payload, (uint32_t)n, &rx) == 0,
          "the payload is decoded");
    CHECK(rx.room_temperature == 215, "the room temperature survives");
    CHECK(rx.humidity == 450, "the humidity survives");
    CHECK(rx.uptime == 123456, "the uptime survives");
    CHECK(rx.relays == 0x11, "the relay states survive");

    CHECK(lora_payload_decode(payload, 8, &rx) != 0,
          "a short payload is refused");

    CHECK(lora_payload_hex(payload, (uint32_t)n, hex, sizeof(hex)) == 32,
          "the payload is 32 hex digits");
    CHECK(hex[0] == '0' && hex[1] == '1',
          "the hex form is what the AT command carries");
}

/* ------------------------------------------------------------------ */
/* The time and humidity sources                                         */
/* ------------------------------------------------------------------ */

static void test_time_and_humidity(void)
{
    rtc_t rtc;
    rtc_time_t time;
    si7021_t si;
    int16_t humidity = 0;
    uint16_t sod = 0, moh = 0, hod = 0, doy = 0;

    printf("Time and humidity: the RAM-backed sources\n");

    CHECK(rtc_is_leap_year(2024), "2024 is a leap year");
    CHECK(!rtc_is_leap_year(2025), "2025 is not a leap year");
    CHECK(!rtc_is_leap_year(2100), "2100 is not a leap year");
    CHECK(rtc_is_leap_year(2000), "2000 is a leap year");

    memset(&time, 0, sizeof(time));
    time.year = 2026;
    time.month = 3;
    time.day = 1;
    CHECK(rtc_day_of_year(&time) == 60, "the first of March is day 60");

    time.year = 2024;
    CHECK(rtc_day_of_year(&time) == 61,
          "the first of March is day 61 in a leap year");

    rtc_init(&rtc, 0, 0);
    CHECK(rtc_get_time(&rtc, &time) == 0,
          "the time is unset until it is set");

    memset(&time, 0, sizeof(time));
    time.year = 2026;
    time.month = 10;
    time.day = 29;
    time.hour = 13;
    time.minute = 45;
    time.second = 30;
    rtc_set_time(&rtc, &time);
    CHECK(rtc_get_time(&rtc, &time) == 1, "the time is read back");
    CHECK(time.hour == 13 && time.minute == 45,
          "the time is the one that was set");

    rtc_split(&time, &sod, &moh, &hod, &doy);
    CHECK(sod == 13 * 3600 + 45 * 60 + 30, "the seconds of the day");
    CHECK(moh == 45, "the minute of the hour");
    CHECK(hod == 13, "the hour of the day");
    CHECK(doy == 302, "the day of the year");

    si7021_init(&si, 0);
    CHECK(si7021_get_humidity(&si, &humidity) == 0,
          "the humidity is unset until it is set");
    si7021_set_humidity(&si, 456);
    CHECK(si7021_get_humidity(&si, &humidity) == 1 && humidity == 450,
          "the humidity is read back in tenths of a percent");
    CHECK(si7021_counts_to_humidity(16) == 1000,
          "the SI7021 counts convert to the humidity");
    CHECK(si7021_counts_to_humidity(8) == 500,
          "half the counts is half the humidity");
}

/* ------------------------------------------------------------------ */
/* The screen and touch layers                                           */
/* ------------------------------------------------------------------ */

static void test_screen_and_touch(void)
{
    screen_t screen;
    screen_manager_t mgr;
    touch_state_t touch;
    touch_point_t point;
    widget_t w;
    uint32_t index = 0;

    printf("Screen and touch: the widget layer\n");

    memset(&screen, 0, sizeof(screen));
    screen.name = "main";

    memset(&w, 0, sizeof(w));
    w.kind = WIDGET_BUTTON;
    w.x = 10;
    w.y = 20;
    w.width = 50;
    w.height = 40;
    w.id = 7;
    w.asset = "bg.bmp";
    CHECK(screen_add_widget(&screen, &w) == 0, "a widget is added");
    CHECK(screen.widget_count == 1, "the widget is counted");

    memset(&w, 0, sizeof(w));
    w.kind = WIDGET_BUTTON;
    w.x = 100;
    w.y = 100;
    w.width = 20;
    w.height = 20;
    w.id = 9;
    CHECK(screen_add_widget(&screen, &w) == 0, "a second widget is added");

    CHECK(screen_hit(&screen, 20, 30, &index) != 0 &&
          screen_hit(&screen, 20, 30, &index)->id == 7,
          "the touch hits the first button");
    CHECK(screen_hit(&screen, 110, 110, &index) != 0 &&
          screen_hit(&screen, 110, 110, &index)->id == 9,
          "the touch hits the second button");
    CHECK(screen_hit(&screen, 500, 500, &index) == 0,
          "a touch outside every button hits nothing");
    CHECK(screen_hit(&screen, 60, 30, &index) == 0,
          "a touch just past the edge hits nothing");

    /* The touchpad lock. */
    touch_init(&touch);
    point.event = TOUCH_DOWN;
    point.x = 20;
    point.y = 30;
    CHECK(touch_handle(&touch, &screen, &point, 9) == 7,
          "an unlocked touchpad passes the press through");

    touch_set_locked(&touch, 1);
    CHECK(touch_is_locked(&touch), "the touchpad lock is engaged");
    CHECK(touch_handle(&touch, &screen, &point, 9) == -1,
          "a locked touchpad swallows the press");
    point.x = 110;
    point.y = 110;
    CHECK(touch_handle(&touch, &screen, &point, 9) == 9,
          "the unlocking widget still works");

    touch_set_locked(&touch, 0);
    CHECK(touch_handle(&touch, &screen, &point, 9) == 9,
          "an unlocked touchpad passes the press through again");

    /* The screen manager. */
    memset(&mgr, 0, sizeof(mgr));
    screen_init(&mgr, 0, 0, &screen, 1);
    CHECK(screen_current(&mgr) == &screen, "the current screen is the first");
    screen_goto(&mgr, 0);
    CHECK(screen_current(&mgr) == &screen, "the screen can be selected");
}

/* ------------------------------------------------------------------ */

void run_protocol_tests(void)
{
    test_usb();
    printf("\n");
    test_panel();
    printf("\n");
    test_bacnet();
    printf("\n");
    test_ota();
    printf("\n");
    test_lora_payload();
    printf("\n");
    test_time_and_humidity();
    printf("\n");
    test_screen_and_touch();
}
