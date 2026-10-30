/*
 * test_main.c - host test harness.
 *
 * The firmware's logic layers (HVAC state machine, BMP decoding, framebuffer
 * blitting, FAT32 asset lookup) are written to be free of hardware
 * dependencies so they can be exercised on the host. This harness builds
 * against the same sources with the host compiler and runs the checks.
 *
 * Build:  make host-test      (from the firmware directory)
 */

#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <stdint.h>

#include "hvac.h"
#include "framebuffer.h"
#include "bmp.h"
#include "asset_store.h"
#include "board.h"
#include "eeprom.h"
#include "selftest.h"
#include "sensors.h"
#include "sensor_bus.h"
#include "at_client.h"
#include "esp_at.h"
#include "lora_at.h"
#include "schedule.h"
#include "meter.h"
#include "faults.h"
#include "scheduler.h"
#include "http_client.h"
#include "mqtt_sn.h"
#include "modbus.h"
#include "digits.h"
#include "fake_i2c.h"
#include "fake_sensor_bus.h"

/* The counters are shared with test_protocols.c. */
int g_failures;
int g_checks;

/* The protocol and UI module checks live in their own file. */
void run_protocol_tests(void);

/* The differential test against the recovered control rules. */
void run_differential_tests(void);

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
/* HVAC state machine                                                  */
/* ------------------------------------------------------------------ */

static void test_hvac_basic(void)
{
    hvac_output_t out;

    printf("HVAC: mode transitions and staging\n");

    hvac_init();
    hvac_set_equipment(HVAC_EQUIP_HEAT_PUMP);
    hvac_set_mode(HVAC_MODE_HEAT);
    hvac_set_occupancy(1);
    hvac_set_room_temp(180);       /* 18.0 C */
    hvac_set_heat_setpoint(220);    /* 22.0 C */
    hvac_set_setpoint(220);
    hvac_set_cool_setpoint(260);

    /* Cold room, heating demanded -> W1 on. */
    hvac_tick(1);
    hvac_output(&out);
    CHECK(out.relay[HVAC_RELAY_W1] == 1, "W1 should be on when heating");
    CHECK(out.relay[HVAC_RELAY_Y1] == 0, "Y1 must be off while heating");

    /* Warm room, no demand -> all off. */
    hvac_set_room_temp(230);
    hvac_tick(1);
    hvac_output(&out);
    CHECK(out.relay[HVAC_RELAY_W1] == 0, "W1 should be off when satisfied");
    CHECK(out.relay[HVAC_RELAY_G] == 0, "fan should be off when idle");

    /* Cooling mode with a warm room -> Y1 on. */
    hvac_set_mode(HVAC_MODE_COOL);
    hvac_set_room_temp(280);
    hvac_tick(1);
    hvac_output(&out);
    CHECK(out.relay[HVAC_RELAY_Y1] == 1, "Y1 should be on when cooling");
    CHECK(out.relay[HVAC_RELAY_W1] == 0, "W1 must be off while cooling");
}

static void test_hvac_hold_holiday_occupancy(void)
{
    hvac_output_t out;

    printf("HVAC: hold, holiday and occupancy gating\n");

    hvac_init();
    hvac_set_mode(HVAC_MODE_HEAT);
    hvac_set_occupancy(1);
    hvac_set_room_temp(180);
    hvac_set_heat_setpoint(220);
    hvac_set_setpoint(220);
    hvac_tick(1);
    hvac_output(&out);
    CHECK(out.relay[HVAC_RELAY_W1] == 1, "baseline heating");

    hvac_set_hold(1);
    hvac_tick(1);
    hvac_output(&out);
    CHECK(out.relay[HVAC_RELAY_W1] == 0, "hold must suppress heating");

    hvac_set_hold(0);
    hvac_set_holiday(1);
    hvac_tick(1);
    hvac_output(&out);
    CHECK(out.relay[HVAC_RELAY_W1] == 0, "holiday must suppress heating");

    hvac_set_holiday(0);
    hvac_set_occupancy(0);
    hvac_tick(1);
    hvac_output(&out);
    CHECK(out.relay[HVAC_RELAY_W1] == 0, "no occupancy must suppress heating");
}

static void test_hvac_stage2(void)
{
    hvac_output_t out;
    unsigned i;

    printf("HVAC: second stage engages only after a soak\n");

    hvac_init();
    hvac_set_equipment(HVAC_EQUIP_2_COMPRESS);
    hvac_set_mode(HVAC_MODE_HEAT);
    hvac_set_occupancy(1);
    hvac_set_room_temp(150);
    hvac_set_heat_setpoint(220);
    hvac_set_setpoint(220);

    hvac_tick(1);
    hvac_output(&out);
    CHECK(out.relay[HVAC_RELAY_W1] == 1, "stage 1 heats");
    CHECK(out.relay[HVAC_RELAY_W2] == 0, "stage 2 must not engage immediately");

    for (i = 0; i < 200; i++)
    {
        hvac_tick(1);
    }
    hvac_output(&out);
    CHECK(out.relay[HVAC_RELAY_W2] == 1,
          "stage 2 should engage after the soak period");
}

static void test_hvac_never_heat_and_cool(void)
{
    hvac_output_t out;

    printf("HVAC: heat and cool are mutually exclusive\n");

    hvac_init();
    hvac_set_equipment(HVAC_EQUIP_HEAT_PUMP);
    hvac_set_mode(HVAC_MODE_HEAT_COOL);
    hvac_set_occupancy(1);
    hvac_set_room_temp(150);
    hvac_set_heat_setpoint(220);
    hvac_set_cool_setpoint(160);
    hvac_set_setpoint(220);
    hvac_tick(1);
    hvac_output(&out);
    CHECK(!(out.relay[HVAC_RELAY_W1] && out.relay[HVAC_RELAY_Y1]),
          "W1 and Y1 must never both be on");
}

static void test_hvac_names(void)
{
    printf("HVAC: mode/equipment names match the firmware's strings\n");

    CHECK(strcmp(hvac_mode_name(HVAC_MODE_HEAT), "Heat") == 0,
          "heat mode name");
    CHECK(strcmp(hvac_mode_name(HVAC_MODE_HEAT_COOL), "Heat & Cool") == 0,
          "heat&cool mode name");
    CHECK(strcmp(hvac_equipment_name(HVAC_EQUIP_HEAT_PUMP), "Heat Pump") == 0,
          "heat pump equipment name");
    CHECK(strcmp(hvac_equipment_name(HVAC_EQUIP_1_COOL_STAGE),
                 "1 Cool Stage") == 0, "1 cool stage name");
}

/* ------------------------------------------------------------------ */
/* Framebuffer and BMP                                                */
/* ------------------------------------------------------------------ */

#define FB_W 64
#define FB_H 32

static void test_framebuffer(void)
{
    uint16_t px[FB_W * FB_H];
    framebuffer_t fb;

    printf("Framebuffer: fill, clipping and blending\n");

    fb_init(&fb, px, FB_W, FB_H, FB_RGB565);
    fb_fill(&fb, RGB565(0, 0, 0));
    CHECK(px[0] == RGB565(0, 0, 0), "fill writes the background");

    fb_fill_rect(&fb, 10, 10, 5, 5, RGB565(255, 0, 0));
    CHECK(px[10 * FB_W + 10] == RGB565(255, 0, 0), "rect fills inside");
    CHECK(px[9 * FB_W + 10] == RGB565(0, 0, 0), "rect does not spill left");

    /* A rect partly off the edge must be clipped, not corrupt memory. */
    fb_fill_rect(&fb, -3, -3, 6, 6, RGB565(0, 255, 0));
    CHECK(px[0] == RGB565(0, 255, 0), "clipped rect covers the visible part");

    /* 50% blend of red over black is roughly half red. */
    fb_fill(&fb, RGB565(0, 0, 0));
    fb_blend_pixel(&fb, 5, 5, RGB565(255, 0, 0), 128);
    {
        uint8_t r = RGB565_RED(px[5 * FB_W + 5]);
        CHECK(r >= 100 && r <= 140, "50%% blend gives mid red (got %u)", r);
    }
}

static void test_bmp_header(void)
{
    /* A minimal 2x1 32bpp BMP built by hand. */
    static uint8_t bmp[54 + 8];
    bmp_info_t info;

    printf("BMP: header parsing\n");

    memset(bmp, 0, sizeof(bmp));
    bmp[0] = 'B';
    bmp[1] = 'M';
    bmp[10] = 54;                     /* data offset */
    bmp[14] = 40;                     /* DIB header size */
    bmp[18] = 2;                      /* width */
    bmp[22] = 1;                      /* height */
    bmp[26] = 1;                      /* planes */
    bmp[28] = 32;                     /* bpp */
    bmp[30] = 0;                      /* BI_RGB */

    CHECK(bmp_parse(bmp, sizeof(bmp), &info) == 0, "parses a valid BMP");
    CHECK(info.width == 2 && info.height == 1, "dimensions");
    CHECK(info.bpp == 32, "bit depth");
    CHECK(info.has_alpha == 1, "32bpp has alpha");
    CHECK(info.row_stride == 8, "stride");

    bmp[0] = 'X';
    CHECK(bmp_parse(bmp, sizeof(bmp), &info) != 0, "rejects a bad signature");
    CHECK(bmp_parse(bmp, 10, &info) != 0, "rejects a truncated file");
}

/* ------------------------------------------------------------------ */
/* Asset store against the generated NOR image                        */
/* ------------------------------------------------------------------ */

static uint8_t *g_nor;
static uint32_t g_nor_size;

static int nor_read(void *ctx, uint32_t addr, uint8_t *out, uint32_t len)
{
    (void)ctx;
    if (addr + len > g_nor_size)
    {
        return -1;
    }
    memcpy(out, g_nor + addr, len);
    return 0;
}

static void test_asset_store(const char *nor_path)
{
    asset_store_t st;
    FILE *f;
    long size;
    uint32_t off;
    uint8_t hdr[54];
    uint32_t sz;

    printf("Asset store: mounting the FAT32 NOR image\n");

    f = fopen(nor_path, "rb");
    if (f == NULL)
    {
        printf("  SKIP: %s not found\n", nor_path);
        return;
    }
    fseek(f, 0, SEEK_END);
    size = ftell(f);
    fseek(f, 0, SEEK_SET);
    g_nor = malloc((size_t)size);
    if (g_nor == NULL || fread(g_nor, 1, (size_t)size, f) != (size_t)size)
    {
        printf("  SKIP: cannot read %s\n", nor_path);
        if (g_nor) { free(g_nor); g_nor = NULL; }
        fclose(f);
        return;
    }
    fclose(f);
    g_nor_size = (uint32_t)size;

    CHECK(asset_store_mount(&st, nor_read, NULL) == 0, "mounts the image");
    CHECK(memcmp(st.volume_label, "AT32", 4) == 0,
          "volume label is AT32 (got \"%s\")", st.volume_label);
    CHECK(st.bytes_per_sector == 512, "bytes per sector");
    CHECK(st.root_cluster >= 2, "root cluster");

    /* The firmware's own path style: leading backslash, backslash
     * separators. */
    off = asset_store_find(&st, "\\White\\105.bmp");
    CHECK(off != ASSET_INVALID, "finds \\White\\105.bmp");
    sz = asset_store_size(&st, "\\White\\105.bmp");
    CHECK(sz > 0, "reports the size of \\White\\105.bmp");

    off = asset_store_find(&st, "logo.bmp");
    CHECK(off != ASSET_INVALID, "finds the root-level logo.bmp");

    off = asset_store_find(&st, "\\fan\\10.bmp");
    CHECK(off != ASSET_INVALID, "finds \\fan\\10.bmp");

    /* A file that does not exist must not resolve. */
    CHECK(asset_store_find(&st, "\\Heatbar\\15.bmp") == ASSET_INVALID,
          "missing asset does not resolve");

    /* Read the header of a resolved file and confirm it is a BMP. */
    off = asset_store_find(&st, "logo.bmp");
    if (off != ASSET_INVALID)
    {
        int32_t w = 0, h = 0;
        uint16_t bpp = 0;

        CHECK(asset_store_read(&st, off, 0, hdr, sizeof(hdr)) == (int)sizeof(hdr),
              "reads the BMP header");
        CHECK(hdr[0] == 'B' && hdr[1] == 'M', "header is a BMP");
        w = (int32_t)(hdr[18] | (hdr[19] << 8) | (hdr[20] << 16) | (hdr[21] << 24));
        h = (int32_t)(hdr[22] | (hdr[23] << 8) | (hdr[24] << 16) | (hdr[25] << 24));
        bpp = (uint16_t)(hdr[28] | (hdr[29] << 8));
        CHECK(w == 200 && h == 100, "logo.bmp is 200x100 (got %dx%d)", w, h);
        CHECK(bpp == 16, "stored as 16bpp (got %u)", bpp);
    }

    free(g_nor);
    g_nor = NULL;
}

/* ------------------------------------------------------------------ */
/* EEPROM over the bit-banged I2C bus                                 */
/* ------------------------------------------------------------------ */

/* The shipped self-test reads two bytes at EEPROM address 1 and expects
 * the signature "#E" (0x23 0x45). The test drives the real driver
 * against a simulated AT24C256, so the bit-level protocol (start, device
 * byte, two address bytes, repeated start, data, stop) is exercised. */
static void test_eeprom(void)
{
    fake_i2c_t fake;
    i2c_bus_t bus;
    uint8_t buf[4];
    uint16_t n;

    printf("EEPROM: AT24C256 over the bit-banged I2C bus\n");

    bus.sda = board_i2c_sda;
    bus.scl = board_i2c_scl;
    fake_i2c_init(&fake, &bus);

    fake.mem[0] = 0x11;
    fake.mem[1] = 0x23;
    fake.mem[2] = 0x45;
    fake.mem[3] = 0x67;

    n = eeprom_read(&bus, 1, buf, 2);
    CHECK(n == 2, "read returns the byte count (got %u)", n);
    CHECK(buf[0] == 0x23 && buf[1] == 0x45,
          "the firmware's \"#E\" signature is read back");
    CHECK(eeprom_check(&bus) == 1, "eeprom_check accepts the \"#E\" signature");
    CHECK(fake.started >= 2, "both address and read phases issue a START "
                             "(got %d)", fake.started);
    CHECK(fake.stopped >= 1, "the transaction ends with a STOP (got %d)",
          fake.stopped);

    n = eeprom_read(&bus, 0, buf, 4);
    CHECK(n == 4, "a four byte read succeeds (got %u)", n);
    CHECK(buf[0] == 0x11 && buf[1] == 0x23 && buf[2] == 0x45 &&
          buf[3] == 0x67, "the four bytes come back in order");

    n = eeprom_read(&bus, EEPROM_SIZE_BYTES - 1, buf, 2);
    CHECK(n == 0, "a read past the end of the 32 KiB device is refused");
    n = eeprom_read(&bus, 0, 0, 4);
    CHECK(n == 0, "a read without a buffer is refused");
    n = eeprom_read(&bus, 0, buf, 0);
    CHECK(n == 0, "a zero length read is refused");

    fake.mem[1] = 0x00;
    CHECK(eeprom_check(&bus) == 0, "a wrong signature is rejected");

    fake.mem[1] = 0x23;
    fake.mem[2] = 0x45;
    CHECK((selftest_eeprom(&bus) & SELFTEST_EEPROM_OK) != 0,
          "the boot self-test accepts the EEPROM");
}

/* ------------------------------------------------------------------ */
/* NTC scaling                                                         */
/* ------------------------------------------------------------------ */

/* The original converts the divider counts to ohms with
 * raw * 12500 / 65536 - 600 and clamps the result to 100..9900 ohms
 * (0x08062504). The temperature curve itself is the rebuild's own. */
static void test_sensors(void)
{
    uint16_t r;
    int16_t t;

    printf("Sensors: NTC divider scaling\n");

    r = ntc_resistance_from_raw(0);
    CHECK(r == NTC_MIN_OHMS, "a zero count clamps to the minimum "
                             "resistance (got %u)", r);
    r = ntc_resistance_from_raw(0xFFFF);
    CHECK(r == NTC_MAX_OHMS, "a full count clamps to the maximum "
                             "resistance (got %u)", r);

    /* Half scale: 32768 * 12500 / 65536 = 6250, minus the 600 ohm
     * offset. */
    r = ntc_resistance_from_raw(0x8000);
    CHECK(r == 5650, "half scale gives 5650 ohms (got %u)", r);

    /* A quarter scale: 16384 * 12500 / 65536 = 3125, minus 600. */
    r = ntc_resistance_from_raw(0x4000);
    CHECK(r == 2525, "a quarter scale gives 2525 ohms (got %u)", r);

    t = ntc_temperature_from_resistance(10000);
    CHECK(t >= 245 && t <= 255, "10 kOhm is about 25 degrees (got %d)", t);
    t = ntc_temperature_from_resistance(NTC_MIN_OHMS);
    CHECK(t > 1000, "a cold sensor reads a high temperature (got %d)", t);
    t = ntc_temperature_from_resistance(NTC_MAX_OHMS);
    CHECK(t > 0 && t < 300, "a hot sensor still reads a plausible "
                             "temperature (got %d)", t);

    t = ntc_temperature_from_raw(0x8000);
    CHECK(t > 0 && t < 400, "a half scale reading is a plausible "
                             "temperature (got %d)", t);
}

/* ------------------------------------------------------------------ */
/* The external measurement bus                                        */
/* ------------------------------------------------------------------ */

/* The original fetches three 16-bit samples over the two-wire bus on
 * GPIOC (data = PC8, clock = PC9) after two command bytes. */
static void test_sensor_bus(void)
{
    fake_sensor_bus_t fake;
    sensor_bus_t bus;
    uint8_t buf[6];

    printf("Measurement bus: two-wire samples over PC8/PC9\n");

    bus.data = board_sensor_data;
    bus.clock = board_sensor_clock;
    fake_sensor_bus_init(&fake, &bus);

    fake.samples[0] = 0x12;
    fake.samples[1] = 0x34;
    fake.samples[2] = 0x56;
    fake.samples[3] = 0x78;
    fake.samples[4] = 0x9A;
    fake.samples[5] = 0xBC;

    CHECK(sensor_bus_read_samples(&bus, buf, 6) == 6,
          "six samples are read back");
    CHECK(buf[0] == 0x12 && buf[1] == 0x34 && buf[2] == 0x56 &&
          buf[3] == 0x78 && buf[4] == 0x9A && buf[5] == 0xBC,
          "the sample bytes come back in order");
    CHECK(fake.started >= 2, "the transaction issues a START per command "
                             "(got %d)", fake.started);
    CHECK(fake.stopped >= 3, "the transaction ends with a STOP (got %d)",
          fake.stopped);
    CHECK(fake.bytes_received == 2, "the two command bytes reach the "
                                    "device (got %d)", fake.bytes_received);
    CHECK(fake.last_byte == 0xFD, "the last command byte is 0xFD "
                                   "(got 0x%02X)", fake.last_byte);

    CHECK(sensor_bus_read_samples(&bus, 0, 6) == 0,
          "a read without a buffer is refused");
}

/* ------------------------------------------------------------------ */
/* AT command clients                                                  */
/* ------------------------------------------------------------------ */

/* The ESP-07S and the LoRa modem speak AT; this exercises the shared
 * line client and the two command dialects. */
static void test_at_clients(void)
{
    at_client_t client;
    char buffer[96];
    const char *line;
    esp_wifi_info_t wifi;
    esp_ip_info_t ip;
    lora_config_t config;
    lora_identity_t identity;
    char cmd[96];

    printf("Connectivity: AT command client\n");

    at_client_init(&client, buffer, sizeof(buffer), 1000);
    CHECK(at_client_command(&client, "AT+CWJAP?") == 0,
          "a command can be started");

    at_client_rx(&client, '+', 0);
    at_client_rx(&client, 'C', 1);
    at_client_rx(&client, 'W', 2);
    at_client_rx(&client, 'J', 3);
    at_client_rx(&client, 'A', 4);
    at_client_rx(&client, 'P', 5);
    at_client_rx(&client, ':', 6);
    at_client_rx(&client, 'M', 7);
    at_client_rx(&client, 'y', 8);
    at_client_rx(&client, 'N', 9);
    at_client_rx(&client, 'e', 10);
    at_client_rx(&client, 't', 11);
    at_client_rx(&client, 'w', 12);
    at_client_rx(&client, 'o', 13);
    at_client_rx(&client, 'r', 14);
    at_client_rx(&client, 'k', 15);
    at_client_rx(&client, ',', 16);
    at_client_rx(&client, '5', 17);
    at_client_rx(&client, '5', 18);
    at_client_rx(&client, '\r', 19);

    CHECK(at_client_poll(&client, &line) == AT_LINE, "a payload line is reported");
    CHECK(esp_parse_wifi(line, &wifi) == 0, "the WiFi answer parses");
    CHECK(wifi.status == ESP_WIFI_ST_CONNECTED, "the module is connected");
    CHECK(strcmp(wifi.ssid, "MyNetwork") == 0, "the SSID is recovered");
    CHECK(wifi.rssi == 55, "the signal strength is recovered");

    at_client_rx(&client, 'O', 20);
    at_client_rx(&client, 'K', 21);
    at_client_rx(&client, '\r', 22);
    CHECK(at_client_poll(&client, &line) == AT_DONE, "the transaction finishes");
    CHECK(at_client_result(&client) == AT_OK, "the result is OK");

    at_client_init(&client, buffer, sizeof(buffer), 1000);
    at_client_command(&client, "AT");
    at_client_rx(&client, 'E', 0);
    at_client_rx(&client, 'R', 1);
    at_client_rx(&client, 'R', 2);
    at_client_rx(&client, 'O', 3);
    at_client_rx(&client, 'R', 4);
    at_client_rx(&client, '\r', 5);
    CHECK(at_client_poll(&client, &line) == AT_DONE, "an error ends the transaction");
    CHECK(at_client_result(&client) == AT_ERROR, "the result is an error");

    at_client_init(&client, buffer, sizeof(buffer), 10);
    at_client_command(&client, "AT");
    at_client_tick(&client, 100);
    CHECK(at_client_poll(&client, &line) == AT_NONE, "a timed out command reports nothing");
    CHECK(client.timeout_count == 1, "the timeout is counted");

    CHECK(esp_parse_ip("+CIFSR:192.168.1.44", &ip) == 0, "the address parses");
    CHECK(strcmp(ip.ip, "192.168.1.44") == 0, "the address is recovered");
    CHECK(esp_parse_version("+GMR:AT firmware 2.2", &(esp_version_t){0}) == 0,
          "the firmware version parses");

    CHECK(esp_cmd_join(cmd, sizeof(cmd), "MyNetwork", "secret") == 0,
          "the join command builds");
    CHECK(strcmp(cmd, "AT+CWJAP_CUR=\"MyNetwork\",\"secret\"") == 0,
          "the join command matches the firmware's form");
    CHECK(esp_cmd_ble_init(cmd, sizeof(cmd), 1) == 0 &&
          strcmp(cmd, "AT+BLEINIT=2") == 0, "BLE is enabled with 2");
    CHECK(esp_cmd_blufi(cmd, sizeof(cmd), 1) == 0 &&
          strcmp(cmd, "AT+BLUFI=1") == 0, "BLUFI is enabled with 1");
    CHECK(esp_cmd_tcp_start(cmd, sizeof(cmd), "10.0.0.1", 1883) == 0 &&
          strcmp(cmd, "AT+CIPSTART=\"TCP\",\"10.0.0.1\",1883") == 0,
          "the TCP start command matches the firmware's form");

    CHECK(lora_parse_config("+CFREQ=7,125000", &config) == 0, "the radio config parses");
    CHECK(config.sf == 7 && config.bw == 125000, "the spreading factor and bandwidth");
    CHECK(lora_parse_config("+CTXP=17", &config) == 0 && config.power_dbm == 17,
          "the transmit power parses");
    CHECK(lora_parse_identity("+CDEVEUI=0011223344556677", &identity) == 0,
          "the device EUI parses");
    CHECK(strcmp(identity.dev_eui, "0011223344556677") == 0, "the EUI is recovered");
    CHECK(lora_cmd_join(cmd, sizeof(cmd), 1, 0, 10) == 0 &&
          strcmp(cmd, "AT+CJOIN=1,0,10") == 0, "the join command matches");
    CHECK(lora_cmd_class(cmd, sizeof(cmd), LORA_CLASS_C) == 0 &&
          strcmp(cmd, "AT+CCLASS=2") == 0, "the class command matches");
    CHECK(lora_cmd_adr(cmd, sizeof(cmd), 1) == 0 &&
          strcmp(cmd, "AT+CADR=1") == 0, "the ADR command matches");
}

/* ------------------------------------------------------------------ */
/* Schedules, metering and faults                                     */
/* ------------------------------------------------------------------ */

static void test_app_logic(void)
{
    schedule_t schedule;
    meter_t meter;
    fault_state_t faults;
    int whole = 0;
    int tenths = 0;

    printf("Application: schedules, metering and faults\n");

    schedule_init(&schedule);
    CHECK(schedule_set_period(&schedule, 0, 200) == 0, "a period is accepted");
    CHECK(schedule_set_period(&schedule, 360, 210) == 0, "a second period is accepted");
    CHECK(schedule_set_period(&schedule, 900, 220) == 0, "a third period is accepted");
    CHECK(schedule_setpoint_at(&schedule, 0) == 200, "the first period is in force");
    CHECK(schedule_setpoint_at(&schedule, 359) == 200, "the first period holds");
    CHECK(schedule_setpoint_at(&schedule, 360) == 210, "the second period starts on time");
    CHECK(schedule_setpoint_at(&schedule, 1000) == 220, "the third period is in force");
    CHECK(schedule_setpoint_at(&schedule, 1439) == 220, "the last period runs to midnight");
    {
        schedule_t wrap;
        schedule_init(&wrap);
        schedule_set_period(&wrap, 360, 210);
        schedule_set_period(&wrap, 900, 220);
        CHECK(schedule_setpoint_at(&wrap, 30) == 220,
              "before the first period the last one of yesterday is in force");
    }
    CHECK(schedule_set_period(&schedule, 5000, 200) != 0,
          "a time outside the day is refused");
    CHECK(schedule_minute_of_day(8, 30) == 510, "08:30 is minute 510");
    CHECK(schedule_minute_of_day(24, 0) == 1439, "an invalid hour is clamped");

    schedule.hold_end = 600;
    CHECK(schedule_in_hold(&schedule, 599) == 1, "the hold is active");
    CHECK(schedule_in_hold(&schedule, 600) == 0, "the hold ends at its end time");
    schedule.hold_end = SCHEDULE_NO_TIME;
    CHECK(schedule_in_hold(&schedule, 100) == 0, "no hold is configured");

    schedule.holiday_start = 1320;
    schedule.holiday_end = 120;
    CHECK(schedule_in_holiday(&schedule, 1380) == 1, "the holiday is active at night");
    CHECK(schedule_in_holiday(&schedule, 60) == 1, "the holiday wraps past midnight");
    CHECK(schedule_in_holiday(&schedule, 600) == 0, "the holiday is over during the day");

    meter_reset(&meter);
    meter_accumulate(&meter, 3600, 1000);
    meter_accumulate(&meter, 3600, 3000);
    CHECK(meter_running_hours(&meter) == 2, "two hours were logged");
    CHECK(meter_average_watts(&meter) == 2000, "the average power is 2 kW");
    meter_power_kw(&meter, &whole, &tenths);
    CHECK(whole == 2 && tenths == 0, "the power prints as 2.0 kW");
    meter_reset(&meter);
    CHECK(meter_average_watts(&meter) == 0, "a reset meter reads zero");

    fault_init(&faults);
    fault_set(&faults, FAULT_BUILTIN_SENSOR);
    CHECK(fault_active(&faults, FAULT_BUILTIN_SENSOR) == 1, "a sensor fault is active");
    CHECK(fault_any(&faults) == 1, "the fault state is not empty");
    CHECK(strcmp(fault_name(FAULT_BUILTIN_SENSOR), "Buildin sensor error") == 0,
          "the fault keeps the firmware's wording");
    fault_clear(&faults, FAULT_BUILTIN_SENSOR);
    CHECK(fault_any(&faults) == 0, "the fault can be cleared");
    fault_set(&faults, FAULT_SYSTEM);
    fault_set(&faults, FAULT_HUMIDITY_SENSOR);
    fault_clear_all(&faults);
    CHECK(fault_any(&faults) == 0, "every fault can be cleared at once");
}

/* ------------------------------------------------------------------ */
/* Cloud clients                                                       */
/* ------------------------------------------------------------------ */

/* The firmware posts form-encoded bodies to the vendor's path family and
 * speaks MQTT-SN to the ESP-07S. */
static void test_cloud_clients(void)
{
    http_request_t request;
    char body[HTTP_BODY_MAX];
    char wire[512];
    mqtt_sn_connect_t connect;
    mqtt_sn_publish_t publish;
    uint8_t packet[64];
    int length;

    printf("Cloud: HTTP request and MQTT-SN packets\n");

    length = http_form_add(body, 0, sizeof(body), "SerialNumber", "SN-001");
    length = http_form_add(body, length, sizeof(body), "RoomDevices",
                           "Room Temperature 1");
    CHECK(length > 0 && strstr(body, "SerialNumber=SN-001") != 0,
          "the form body carries the first pair");
    CHECK(strstr(body, "Room%20Devices") != 0 ||
          strstr(body, "Room+Devices") != 0 ||
          strstr(body, "RoomDevices") != 0,
          "the form body percent-encodes the value");

    http_build_post(&request, "www.brande.com",
                    "/HEader/SerialNumber/RoomDevices/RoomTemperature1/225",
                    body);
    CHECK(strcmp(request.method, "POST") == 0, "the method is POST");
    CHECK(strcmp(request.content_type,
                 "application/x-www-form-urlencoded") == 0,
          "the content type matches the firmware");
    CHECK(request.body_length == (int)strlen(body),
          "the content length matches the body");

    length = http_render(&request, wire, sizeof(wire));
    CHECK(length > 0, "the request renders");
    CHECK(strstr(wire, "HTTP/1.1") != 0, "the request line is HTTP/1.1");
    CHECK(strstr(wire, "Host: www.brande.com") != 0, "the host header is set");
    CHECK(strstr(wire, "Connection: close") != 0, "the connection header matches");
    CHECK(strstr(wire, "/HEader/SerialNumber/RoomDevices/") != 0,
          "the path uses the firmware's family");

    memset(&connect, 0, sizeof(connect));
    connect.flags = 0x02;
    connect.protocol_id = 0x01;
    connect.duration = 60;
    snprintf((char *)connect.client_id, sizeof(connect.client_id), "MC6");
    length = mqtt_sn_encode_connect(packet, sizeof(packet), &connect);
    CHECK(length == 9, "the CONNECT packet has the expected length "
                        "(got %d)", length);
    CHECK(packet[0] == MQTT_SN_CONNECT, "the packet type is CONNECT");
    CHECK(packet[2] == 0x02 && packet[3] == 0x01,
          "the flags and protocol id are carried");

    memset(&publish, 0, sizeof(publish));
    publish.flags = 0x00;
    publish.topic_id = 0;
    publish.message_id = 0x1234;
    publish.topic = "t";
    publish.payload = (const uint8_t *)"{\"temp\":210}";
    publish.payload_length = 11;
    length = mqtt_sn_encode_publish(packet, sizeof(packet), &publish);
    CHECK(length == 18, "the PUBLISH packet has the expected length "
                        "(got %d)", length);
    CHECK(packet[0] == MQTT_SN_PUBLISH, "the packet type is PUBLISH");
    CHECK(packet[4] == 0x12 && packet[5] == 0x34, "the message id is big endian");
    CHECK(memcmp(packet + 6, "t", 1) == 0, "the topic is carried");
    CHECK(mqtt_sn_message_type(packet, length) == MQTT_SN_PUBLISH,
          "the message type can be read back");

    length = mqtt_sn_encode_subscribe(packet, sizeof(packet), 0x00, 0x0007, "s");
    CHECK(length == 6, "the SUBSCRIBE packet has the expected length "
                       "(got %d)", length);
    CHECK(packet[0] == MQTT_SN_SUBSCRIBE, "the packet type is SUBSCRIBE");
    CHECK(packet[3] == 0x00 && packet[4] == 0x07, "the message id is carried");
}

/* ------------------------------------------------------------------ */
/* Modbus                                                              */
/* ------------------------------------------------------------------ */

/* The firmware publishes its points in a table at 0x08090528; this
 * exercises the rebuild's register map and the server core over both
 * transports. */
static void test_modbus(void)
{
    uint8_t request[32];
    uint8_t response[64];
    int length;
    int i;

    printf("Modbus: register map and server core\n");

    CHECK(modbus_find(0, MODBUS_POINT_INPUT) != 0,
          "the room temperature point is in the map");
    CHECK(strcmp(modbus_find(0, MODBUS_POINT_INPUT)->name,
                 "Actual_Room_temperature") == 0,
          "the point keeps the firmware's name");
    CHECK(modbus_find(100, MODBUS_POINT_INPUT) == 0,
          "an unknown address is not in the map");

    /* Modbus TCP: read two input registers starting at the relays. */
    memset(request, 0, sizeof(request));
    request[0] = 0;
    request[1] = 0;          /* transaction id */
    request[2] = 0;
    request[3] = 0;          /* protocol id */
    request[4] = 0;
    request[5] = 6;          /* length */
    request[6] = 1;          /* unit id */
    request[7] = 0x04;       /* read input registers */
    request[8] = 0;
    request[9] = 2;          /* start address (relay block) */
    request[10] = 0;
    request[11] = 2;         /* count */

    length = modbus_tcp_process(request, 12, response, sizeof(response));
    CHECK(length == 13, "the TCP response has the expected length (got %d)",
          length);
    CHECK(response[7] == 0x04, "the response repeats the function code");
    CHECK(response[8] == 4, "the byte count is two registers");
    CHECK(response[4] == 0 && response[5] == 7,
          "the MBAP length is updated");

    /* Modbus RTU: the same request with the address byte and a CRC. */
    memset(request, 0, sizeof(request));
    request[0] = 1;          /* unit id */
    request[1] = 0x04;
    request[2] = 0;
    request[3] = 0;
    request[4] = 0;
    request[5] = 2;
    request[6] = 0;
    request[7] = 0;
    request[8] = 2;
    {
        uint16_t crc = modbus_crc16(request, 9);

        request[9] = (uint8_t)(crc & 0xFF);
        request[10] = (uint8_t)(crc >> 8);
    }
    length = modbus_rtu_process(request, 11, response, sizeof(response));
    CHECK(length > 0, "the RTU response is built");
    CHECK(response[0] == 1, "the response keeps the unit id");
    CHECK(response[1] == 0x04, "the response repeats the function code");
    CHECK(response[2] == 4, "the byte count is two registers");
    {
        uint16_t crc = modbus_crc16(response, length - 2);

        CHECK((uint8_t)(crc & 0xFF) == response[length - 2] &&
              (uint8_t)(crc >> 8) == response[length - 1],
              "the response carries the right CRC");
    }

    /* Writing a holding register updates the map. */
    memset(request, 0, sizeof(request));
    request[0] = 1;
    request[1] = 0x06;
    request[2] = 0;
    request[3] = 0;          /* address 0 = HeatSetTemp */
    request[4] = 0;
    request[5] = 210;        /* 21.0 degrees in tenths */
    {
        uint16_t crc = modbus_crc16(request, 6);

        request[6] = (uint8_t)(crc & 0xFF);
        request[7] = (uint8_t)(crc >> 8);
    }
    length = modbus_rtu_process(request, 8, response, sizeof(response));
    CHECK(length > 0, "the write is answered");
    for (i = 0; i < (int)g_modbus_point_count; i++)
    {
        if (strcmp(g_modbus_points[i].name, "HeatSetTemp") == 0)
        {
            CHECK(g_modbus_points[i].raw == 2100,
                  "the written value is scaled into the map (got %u)",
                  g_modbus_points[i].raw);
        }
    }

    /* An unknown function is answered with an exception. */
    memset(request, 0, sizeof(request));
    request[0] = 1;
    request[1] = 0x42;
    {
        uint16_t crc = modbus_crc16(request, 2);

        request[2] = (uint8_t)(crc & 0xFF);
        request[3] = (uint8_t)(crc >> 8);
    }
    length = modbus_rtu_process(request, 4, response, sizeof(response));
    CHECK(length == 5, "the exception answer is five bytes (got %d)", length);
    CHECK(response[1] == 0xC2, "the exception sets the error bit");
    CHECK(response[2] == 0x01, "the exception code is the illegal function");
}

/* ------------------------------------------------------------------ */
/* Numeric glyphs                                                      */
/* ------------------------------------------------------------------ */

/* The UI draws its numbers from the icon set: setnum0..9, mtnum0..9 and
 * the other families. */
static void test_digits(const char *nor_path)
{
    asset_store_t store;
    framebuffer_t fb;
    static uint16_t pixels[64 * 48];
    static uint8_t *image;
    const char *name;
    FILE *f;
    long size;

    printf("UI: numeric glyphs\n");

    CHECK(digit_asset_name(DIGIT_STYLE_SET, 3) != 0 &&
          strcmp(digit_asset_name(DIGIT_STYLE_SET, 3), "setnum3.bmp") == 0,
          "the setnum family is named as the icon set does");
    CHECK(strcmp(digit_asset_name(DIGIT_STYLE_MTLN, 0), "mtlnum0.bmp") == 0,
          "the mtlnum family is named as the icon set does");
    CHECK(digit_asset_name(DIGIT_STYLE_SET, 12) == 0,
          "a digit outside 0..9 has no name");

    /* Every numeric family the firmware references must be nameable. */
    CHECK(strcmp(digit_asset_name(DIGIT_STYLE_BIG, 4), "b4.bmp") == 0,
          "the big number family is named as the icon set does");
    CHECK(strcmp(digit_asset_name(DIGIT_STYLE_NB, 4), "nb4.bmp") == 0,
          "the number bar family is named as the icon set does");
    CHECK(strcmp(digit_asset_name(DIGIT_STYLE_SCB, 4), "scb4.bmp") == 0,
          "the schedule column bar family is named as the icon set does");
    CHECK(strcmp(digit_asset_name(DIGIT_STYLE_SHB, 4), "shb4.bmp") == 0,
          "the schedule hour bar family is named as the icon set does");
    CHECK(strcmp(digit_asset_name(DIGIT_STYLE_SETLN, 4), "setln4.bmp") == 0,
          "the setpoint line family is named as the icon set does");
    CHECK(strcmp(digit_sign_asset_name(DIGIT_STYLE_MT), "mtnum00.bmp") == 0,
          "the main temperature sign glyph is mtnum00.bmp");
    CHECK(strcmp(digit_sign_asset_name(DIGIT_STYLE_BIG), "b00.bmp") == 0,
          "the big number sign glyph is b00.bmp");
    CHECK(strcmp(digit_dot_asset_name(DIGIT_STYLE_SET), "setndot.bmp") == 0,
          "the setpoint decimal point is setndot.bmp");
    CHECK(strcmp(digit_dot_asset_name(DIGIT_STYLE_MT), "mtdot.bmp") == 0,
          "the main temperature decimal point is mtdot.bmp");
    CHECK(digit_sign_asset_name(DIGIT_STYLE_SET) == 0,
          "the setpoint family has no sign glyph");
    CHECK(strcmp(wifi_bar_asset_name(3), "wi3.bmp") == 0,
          "the wifi bars are named wi0..wi4");
    CHECK(wifi_bar_asset_name(5) == 0,
          "a wifi level outside 0..4 has no name");

    f = fopen(nor_path, "rb");
    if (f == NULL)
    {
        printf("  (no NOR image, skipping the drawing check)\n");
        return;
    }
    fseek(f, 0, SEEK_END);
    size = ftell(f);
    fseek(f, 0, SEEK_SET);
    image = (uint8_t *)malloc((size_t)size);
    if (image == NULL || fread(image, 1, (size_t)size, f) != (size_t)size)
    {
        if (image != NULL) { free(image); image = NULL; }
        fclose(f);
        printf("  (no NOR image, skipping the drawing check)\n");
        return;
    }
    fclose(f);
    g_nor = image;
    g_nor_size = (uint32_t)size;

    if (asset_store_mount(&store, nor_read, NULL) != 0)
    {
        free(image);
        g_nor = NULL;
        printf("  (no NOR image, skipping the drawing check)\n");
        return;
    }

    fb_init(&fb, pixels, 64, 48, 0x0000);
    name = digit_asset_name(DIGIT_STYLE_SET, 7);
    CHECK(asset_store_find(&store, name) != ASSET_INVALID,
          "the digit %s is in the image", name);
    CHECK(digit_blit(&fb, &store, DIGIT_STYLE_SET, 7, 0, 0) == 0,
          "the digit is drawn");
    CHECK(digit_blit(&fb, &store, DIGIT_STYLE_SET, 7, -20, -20) == 0,
          "a digit drawn off the edge is clipped, not rejected");
    CHECK(digit_blit(&fb, &store, DIGIT_STYLE_SET, 7, 0, 0) == 0,
          "the digit is drawn twice");

    /* The glyphs are streamed out of the NOR, so the sizes come from the
     * asset's own header rather than from a table. */
    {
        int32_t w = 0;
        int32_t h = 0;
        CHECK(digit_measure(&store, DIGIT_STYLE_SET, 7, &w, &h) == 0 &&
              w == 22 && h == 33,
              "the setpoint digit measures 22x33");
        CHECK(digit_measure(&store, DIGIT_STYLE_MT, 0, &w, &h) == 0 &&
              w == 40 && h == 62,
              "the main temperature digit measures 40x62");
        CHECK(number_width(&store, DIGIT_STYLE_SET, 3) == 66,
              "three setpoint digits are 66 pixels wide");
    }

    /* The decimal point and the sign glyphs draw too. */
    CHECK(digit_dot_blit(&fb, &store, DIGIT_STYLE_SET, 30, 20) == 0,
          "the decimal point is drawn");
    CHECK(digit_sign_blit(&fb, &store, DIGIT_STYLE_MT, 0, 0) == 0,
          "the sign glyph is drawn");
    CHECK(digit_background_blit(&fb, &store, 0, 0) == 0,
          "the digit background is drawn");

    /* A whole number, with a decimal point and a sign. */
    CHECK(number_blit(&fb, &store, DIGIT_STYLE_SET, 235, 60, 0, 1,
                      DIGIT_ALIGN_RIGHT, 2) == 0,
          "23.5 is drawn right aligned");
    CHECK(number_blit(&fb, &store, DIGIT_STYLE_MT, -50, 60, 0, 1,
                      DIGIT_ALIGN_RIGHT, 2) == 0,
          "-5.0 is drawn right aligned");
    CHECK(number_blit(&fb, &store, DIGIT_STYLE_SET, 21, 0, 0, 0,
                      DIGIT_ALIGN_LEFT, 3) == 0,
          "021 is drawn left aligned, zero padded");
    CHECK(number_blit(&fb, &store, DIGIT_STYLE_SET, 21, 0, 0, 2,
                      DIGIT_ALIGN_LEFT, 1) != 0,
          "a two decimal place number is refused");

    free(image);
    g_nor = NULL;
}

/* ------------------------------------------------------------------ */
/* Scheduler                                                           */
/* ------------------------------------------------------------------ */

static int g_seen_messages;
static uint32_t g_last_param;

static void scheduler_test_handler(const scheduler_message_t *message, void *context)
{
    (void)context;
    g_seen_messages++;
    g_last_param = message->param;
}

static void test_scheduler(void)
{
    scheduler_t scheduler;
    scheduler_message_t message;

    printf("Scheduler: tick and window messages\n");

    scheduler_init(&scheduler);
    scheduler.handler = scheduler_test_handler;

    scheduler_tick(&scheduler);
    scheduler_tick(&scheduler);
    CHECK(scheduler.ticks == 2, "the tick counter advances");

    CHECK(scheduler_post(&scheduler, WM_AT_TX, 7, 0) == 0, "a message is queued");
    CHECK(scheduler_post(&scheduler, WM_UI_REDRAW, 9, 0) == 0, "a second message is queued");
    CHECK(scheduler_next(&scheduler, &message) == 1, "the first message comes out");
    CHECK(message.message == WM_AT_TX, "the messages keep their order");
    CHECK(message.param == 7, "the parameter survives");
    CHECK(scheduler_next(&scheduler, &message) == 1, "the second message comes out");
    CHECK(message.message == WM_UI_REDRAW, "the second message is the redraw");

    g_seen_messages = 0;
    scheduler_post(&scheduler, WM_SENSOR_POLL, 3, 0);
    scheduler_run(&scheduler);
    CHECK(g_seen_messages == 1, "the handler runs for the queued message");
    CHECK(g_last_param == 3, "the handler sees the parameter");

    /* The queue is bounded: overflow is counted, not silently lost. */
    {
        int i;
        scheduler_init(&scheduler);
        for (i = 0; i < SCHEDULER_QUEUE_SIZE + 4; i++)
        {
            scheduler_post(&scheduler, WM_TICK, (uint32_t)i, 0);
        }
        CHECK(scheduler.dropped == 5,
              "the overflow is counted (the queue holds %d)",
              SCHEDULER_QUEUE_SIZE - 1);
    }
}

/* ------------------------------------------------------------------ */
/* Board pin map                                                       */
/* ------------------------------------------------------------------ */

/* The pin assignments below are [F] facts recovered from the shipped
 * firmware: the point-registry relay handlers (0x08042674, 0x080426A8,
 * 0x08013268, 0x0801329C, 0x0803EA9C) each read one pin, the board
 * output init at 0x0807DC5C configures the same five, the EEPROM I2C
 * driver (0x08042BD8) drives PG6/PG8, and the external measurement bus
 * (0x08061958-0x08061B70) drives PC8/PC9. This test keeps them from
 * drifting. */
static void test_board_map(void)
{
    printf("Board pin map (relays and sensor buses)\n");

    CHECK(board_relay_w1.port == GPIOA && board_relay_w1.pin == 3,
          "W1 relay is PA3");
    CHECK(board_relay_w2.port == GPIOA && board_relay_w2.pin == 1,
          "W2 relay is PA1");
    CHECK(board_relay_y1.port == GPIOB && board_relay_y1.pin == 13,
          "Y1 relay is PB13");
    CHECK(board_relay_y2.port == GPIOA && board_relay_y2.pin == 2,
          "Y2 relay is PA2");
    CHECK(board_relay_g.port == GPIOB && board_relay_g.pin == 14,
          "Fan relay is PB14");

    CHECK(board_i2c_sda.port == GPIOG && board_i2c_sda.pin == 6,
          "EEPROM I2C SDA is PG6");
    CHECK(board_i2c_scl.port == GPIOG && board_i2c_scl.pin == 8,
          "EEPROM I2C SCL is PG8");
    CHECK(board_sensor_data.port == GPIOC && board_sensor_data.pin == 8,
          "measurement bus data is PC8");
    CHECK(board_sensor_clock.port == GPIOC && board_sensor_clock.pin == 9,
          "measurement bus clock is PC9");
    CHECK(BOARD_ADC_BASE_ADDR == 0x40013C00UL,
          "ADC is at the AT32 address 0x40013C00");
}

/* ------------------------------------------------------------------ */

int main(int argc, char **argv)
{
    const char *nor_path = (argc > 1) ? argv[1] : "../build/nor-image.bin";

    printf("Falcon HP firmware - host test harness\n");
    printf("======================================\n\n");

    test_hvac_basic();
    test_hvac_hold_holiday_occupancy();
    test_hvac_stage2();
    test_hvac_never_heat_and_cool();
    test_hvac_names();
    printf("\n");
    test_framebuffer();
    printf("\n");
    test_bmp_header();
    printf("\n");
    test_asset_store(nor_path);
    printf("\n");
    test_eeprom();
    printf("\n");
    test_sensors();
    printf("\n");
    test_sensor_bus();
    printf("\n");
    test_at_clients();
    printf("\n");
    test_app_logic();
    printf("\n");
    test_scheduler();
    printf("\n");
    test_cloud_clients();
    printf("\n");
    test_modbus();
    printf("\n");
    test_digits(nor_path);
    printf("\n");
    run_protocol_tests();
    printf("\n");
    run_differential_tests();
    printf("\n");
    test_board_map();

    printf("\n======================================\n");
    printf("%d checks, %d failures\n", g_checks, g_failures);
    return g_failures ? 1 : 0;
}
