/*
 * panel.h - the 480x320 colour display.
 *
 * What the firmware proves about the panel:
 *
 *   - It is a 480x320 colour panel: bg.bmp in the supplied icon set is
 *     exactly 480x320, and the panel carries its own GRAM (a full
 *     480x320 RGB565 framebuffer is 300 KiB and the MCU has 96 KiB of
 *     SRAM, so the framebuffer cannot live in RAM).
 *   - It is driven over SPI. Two SPI peripherals appear in the image,
 *     SPI1 (0x40013400, two references) and SPI3 (0x40003c00, four).
 *     SPI3 is the panel bus: it is the one with a byte-wide write
 *     helper (0x0807e730, which writes SPI3->DR through the pointer
 *     held at 0x2000d61c) and the one with the transfer routine.
 *   - The controller is initialised with the standard MIPI-DCS command
 *     set: 0x36 (MADCTL), 0x3a (PIXDIM), 0x11 (SLPOUT) and 0x29
 *     (DISPON) are all present in the image.
 *   - The assets are stored as RGB565 in the NOR image, so the panel's
 *     pixel format is 16 bits per pixel.
 *
 * The controller part number is *not* recoverable from the image (no
 * controller name string exists, and the init sequence is built in
 * code rather than from a table), so this driver implements the
 * standard MIPI-DCS subset the firmware uses and leaves the
 * controller-specific power-on sequence as a table the integrator can
 * fill in. See docs/open-questions.md question 7.
 */

#ifndef PANEL_H
#define PANEL_H

#include <stdint.h>

#define PANEL_WIDTH        480
#define PANEL_HEIGHT       320
#define PANEL_PIXEL_BYTES  2

/* The MIPI-DCS commands the firmware uses. */
#define DCS_NOP        0x00
#define DCS_SWRESET    0x01
#define DCS_SLPOUT     0x11
#define DCS_INVOFF     0x20
#define DCS_DISPON     0x29
#define DCS_CASET      0x2A
#define DCS_RASET      0x2B
#define DCS_MADCTL     0x36
#define DCS_IDMOFF     0x38
#define DCS_COLMOD     0x3A
#define DCS_PIXDIM     0x3A

/* MADCTL bits. */
#define MADCTL_MY  0x80
#define MADCTL_MX  0x40
#define MADCTL_MV  0x20
#define MADCTL_ML  0x10
#define MADCTL_RGB 0x00
#define MADCTL_BGR 0x08

/* The bus the panel is on. */
typedef struct
{
    /* Writes `len` bytes to the panel's SPI data register. */
    void (*write)(void *ctx, const uint8_t *data, uint32_t len);
    /* Milliseconds since boot, for the controller's timing delays. */
    uint32_t (*millis)(void *ctx);
    void *ctx;
} panel_bus_t;

typedef struct
{
    const panel_bus_t *bus;
    void              *ctx;
    uint16_t           width;
    uint16_t           height;
    uint8_t            xoffset;
    uint8_t            yoffset;
    uint8_t            madctl;
    uint8_t            colmod;   /* the PIXDIM argument */
    uint8_t            inversion; /* 1: display inversion on */
    int                initialised;
    /* The controller's power-on sequence. Each entry is a command
     * followed by its arguments, the last byte of the group being the
     * argument count. A NULL table uses the default sequence. */
    const uint8_t *init_table;
    uint32_t       init_length;
} panel_t;

/* Prepares the panel: resets the controller and programs the display. */
void panel_init(panel_t *panel, const panel_bus_t *bus, void *ctx);

/* Writes one command with no arguments. */
void panel_command(panel_t *panel, uint8_t command);

/* Writes one command with `count` arguments. */
void panel_command_data(panel_t *panel, uint8_t command,
                         const uint8_t *data, uint32_t count);

/* Selects the address window. */
void panel_set_window(panel_t *panel, int32_t x, int32_t y,
                      int32_t w, int32_t h);

/* Writes raw RGB565 pixels. */
void panel_write_pixels(panel_t *panel, const uint8_t *pixels, uint32_t len);

/* Turns the display on or off. */
void panel_display_on(panel_t *panel, int on);

#endif /* PANEL_H */
