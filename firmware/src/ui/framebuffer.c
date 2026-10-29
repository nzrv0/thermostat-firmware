/*
 * framebuffer.c - RGB565 framebuffer and blitting.
 */

#include "framebuffer.h"

void fb_init(framebuffer_t *fb, uint16_t *pixels, int32_t w, int32_t h,
             fb_format_t fmt)
{
    fb->pixels = pixels;
    fb->width  = w;
    fb->height = h;
    fb->stride = w;
    fb->format = fmt;
}

void fb_fill(framebuffer_t *fb, uint16_t colour)
{
    int32_t y, x;

    for (y = 0; y < fb->height; y++)
    {
        uint16_t *row = fb->pixels + (int32_t)y * fb->stride;
        for (x = 0; x < fb->width; x++)
        {
            row[x] = colour;
        }
    }
}

void fb_fill_rect(framebuffer_t *fb, int32_t x, int32_t y,
                  int32_t w, int32_t h, uint16_t colour)
{
    int32_t yy, xx;

    for (yy = y; yy < y + h; yy++)
    {
        if (yy < 0 || yy >= fb->height)
        {
            continue;
        }
        for (xx = x; xx < x + w; xx++)
        {
            if (xx < 0 || xx >= fb->width)
            {
                continue;
            }
            fb->pixels[(int32_t)yy * fb->stride + xx] = colour;
        }
    }
}

void fb_pixel(framebuffer_t *fb, int32_t x, int32_t y, uint16_t colour)
{
    if (x < 0 || x >= fb->width || y < 0 || y >= fb->height)
    {
        return;
    }
    fb->pixels[(int32_t)y * fb->stride + x] = colour;
}

void fb_blend_pixel(framebuffer_t *fb, int32_t x, int32_t y,
                    uint16_t colour, uint8_t opacity)
{
    uint16_t *p;
    uint16_t dst;
    uint32_t r, g, b, dr, dg, db;
    uint32_t a;

    if (x < 0 || x >= fb->width || y < 0 || y >= fb->height)
    {
        return;
    }
    if (opacity == 0)
    {
        return;
    }
    if (opacity == 255)
    {
        fb->pixels[(int32_t)y * fb->stride + x] = colour;
        return;
    }

    p = &fb->pixels[(int32_t)y * fb->stride + x];
    dst = *p;

    r  = RGB565_RED(colour);
    g  = RGB565_GREEN(colour);
    b  = RGB565_BLUE(colour);
    dr = RGB565_RED(dst);
    dg = RGB565_GREEN(dst);
    db = RGB565_BLUE(dst);

    a = opacity;
    r  = (r  * a + dr * (255U - a)) / 255U;
    g  = (g  * a + dg * (255U - a)) / 255U;
    b  = (b  * a + db * (255U - a)) / 255U;

    *p = RGB565(r, g, b);
}

void fb_blit_bgra(framebuffer_t *fb, const uint8_t *src, int32_t src_w,
                  int32_t src_h, int32_t src_stride, int32_t x, int32_t y,
                  int32_t clip_x, int32_t clip_y, int32_t clip_w, int32_t clip_h)
{
    int32_t sy, sx;
    int32_t cx0 = clip_x;
    int32_t cy0 = clip_y;
    int32_t cx1 = clip_x + clip_w;
    int32_t cy1 = clip_y + clip_h;

    for (sy = 0; sy < src_h; sy++)
    {
        const uint8_t *row = src + (int32_t)sy * src_stride;
        int32_t dy = y + sy;

        if (dy < cy0 || dy >= cy1)
        {
            continue;
        }
        if (dy < 0 || dy >= fb->height)
        {
            continue;
        }

        for (sx = 0; sx < src_w; sx++)
        {
            int32_t dx = x + sx;
            uint8_t b, g, r, a;

            if (dx < cx0 || dx >= cx1)
            {
                continue;
            }
            if (dx < 0 || dx >= fb->width)
            {
                continue;
            }

            b = row[sx * 4 + 0];
            g = row[sx * 4 + 1];
            r = row[sx * 4 + 2];
            a = row[sx * 4 + 3];

            if (a == 0)
            {
                continue;
            }
            fb_blend_pixel(fb, dx, dy, RGB565(r, g, b), a);
        }
    }
}
