/*
 * framebuffer.h - RGB565 framebuffer and blitting.
 *
 * The panel is 480x320 (bg.bmp in the supplied asset set is exactly
 * 480x320, which fixes the geometry). A full framebuffer at 16bpp is
 * 480 * 320 * 2 = 307,200 bytes, which does not fit in the 96 KiB SRAM
 * alongside everything else, so the display path is expected to be
 * driven from the external NOR through FSMC rather than from a
 * full-screen RAM buffer.
 *
 * This module therefore provides:
 *   - a small, windowed framebuffer that can be backed by RAM for tests
 *     and by the caller for hardware,
 *   - primitives (fill, blit, blend) that take a destination pointer
 *     plus a stride, so the same code works on a RAM buffer and on a
 *     NOR-resident surface.
 */

#ifndef FRAMEBUFFER_H
#define FRAMEBUFFER_H

#include <stdint.h>

typedef enum
{
    FB_RGB565 = 0,
    FB_BGR888 = 1
} fb_format_t;

typedef struct
{
    uint16_t    *pixels;     /* destination pixels */
    int32_t      width;
    int32_t      height;
    int32_t      stride;     /* in pixels */
    fb_format_t  format;
} framebuffer_t;

/* Colours in RGB565. */
#define RGB565(r, g, b) \
    ((uint16_t)((((r) & 0xF8) << 8) | (((g) & 0xFC) << 3) | ((b) >> 3)))

#define RGB565_RED(c)     ((uint8_t)(((c) >> 8) & 0xF8))
#define RGB565_GREEN(c)   ((uint8_t)(((c) >> 3) & 0xFC))
#define RGB565_BLUE(c)    ((uint8_t)((c) << 3) & 0xF8)

void fb_init(framebuffer_t *fb, uint16_t *pixels, int32_t w, int32_t h,
             fb_format_t fmt);
void fb_fill(framebuffer_t *fb, uint16_t colour);
void fb_fill_rect(framebuffer_t *fb, int32_t x, int32_t y,
                  int32_t w, int32_t h, uint16_t colour);
void fb_pixel(framebuffer_t *fb, int32_t x, int32_t y, uint16_t colour);
void fb_blend_pixel(framebuffer_t *fb, int32_t x, int32_t y,
                    uint16_t colour, uint8_t opacity);

/* Copy a 32bpp BGRA source (bottom-up rows are handled by the caller
 * providing rows top-down) with alpha blending. */
void fb_blit_bgra(framebuffer_t *fb, const uint8_t *src, int32_t src_w,
                  int32_t src_h, int32_t src_stride, int32_t x, int32_t y,
                  int32_t clip_x, int32_t clip_y, int32_t clip_w, int32_t clip_h);

#endif /* FRAMEBUFFER_H */
