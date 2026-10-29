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

static int g_failures;
static int g_checks;

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

    printf("\n======================================\n");
    printf("%d checks, %d failures\n", g_checks, g_failures);
    return g_failures ? 1 : 0;
}
