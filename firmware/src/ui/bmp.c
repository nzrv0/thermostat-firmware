/*
 * bmp.c - Windows BMP decoder.
 *
 * See bmp.h for the asset format description.
 */

#include "bmp.h"
#include <string.h>

#define BMP_FILE_HEADER_SIZE 14
#define BMP_INFO_HEADER_SIZE 40

static uint32_t rd32(const uint8_t *p)
{
    return (uint32_t)p[0] | ((uint32_t)p[1] << 8) |
           ((uint32_t)p[2] << 16) | ((uint32_t)p[3] << 24);
}

static uint32_t rd16(const uint8_t *p)
{
    return (uint32_t)p[0] | ((uint32_t)p[1] << 8);
}

int bmp_parse(const uint8_t *data, uint32_t len, bmp_info_t *out)
{
    uint32_t data_off;
    int32_t  width, height;
    uint32_t bpp, compression;
    uint32_t header_size;

    if (data == NULL || out == NULL || len < BMP_FILE_HEADER_SIZE + 4)
    {
        return -1;
    }
    if (data[0] != 'B' || data[1] != 'M')
    {
        return -1;
    }

    data_off = rd32(data + 10);
    header_size = rd32(data + 14);

    if (header_size < BMP_INFO_HEADER_SIZE ||
        len < BMP_FILE_HEADER_SIZE + header_size)
    {
        return -1;
    }

    width  = (int32_t)rd32(data + 18);
    height = (int32_t)rd32(data + 22);
    bpp    = rd16(data + 28);
    compression = rd32(data + 30);

    /* Only uncompressed BI_RGB is present in the asset set. */
    if (compression != 0)
    {
        return -2;
    }
    if (bpp != 24 && bpp != 32)
    {
        return -3;
    }
    if (width <= 0 || height == 0)
    {
        return -4;
    }
    /* A negative height means the rows are stored top-down. */
    if (height < 0)
    {
        height = -height;
    }

    out->width       = width;
    out->height      = height;
    out->bpp         = bpp;
    out->data_offset = data_off;
    out->has_alpha   = (bpp == 32) ? 1 : 0;

    if (bpp == 32)
    {
        out->row_stride = (uint32_t)width * 4U;
    }
    else
    {
        /* 24bpp rows are padded to a 4-byte boundary. */
        out->row_stride = (((uint32_t)width * 3U) + 3U) & ~3U;
    }

    if (data_off + out->row_stride * (uint32_t)height > len)
    {
        return -5;
    }

    return 0;
}

int bmp_draw(const uint8_t *data, uint32_t len, framebuffer_t *fb,
             int32_t x, int32_t y)
{
    bmp_info_t info;
    int32_t sy;
    int rc;

    rc = bmp_parse(data, len, &info);
    if (rc != 0)
    {
        return rc;
    }

    for (sy = 0; sy < info.height; sy++)
    {
        /* BMP rows are stored bottom-up, so visual row 0 is the last
         * row in the file. */
        const uint8_t *row = data + info.data_offset +
                            (uint32_t)(info.height - 1 - sy) * info.row_stride;
        int32_t dy = y + sy;
        int32_t sx;

        if (dy < 0 || dy >= fb->height)
        {
            continue;
        }

        if (info.bpp == 32)
        {
            for (sx = 0; sx < info.width; sx++)
            {
                uint8_t b = row[sx * 4 + 0];
                uint8_t g = row[sx * 4 + 1];
                uint8_t r = row[sx * 4 + 2];
                uint8_t a = row[sx * 4 + 3];
                int32_t dx = x + sx;

                if (dx < 0 || dx >= fb->width)
                {
                    continue;
                }
                if (a == 0)
                {
                    continue;
                }
                fb_blend_pixel(fb, dx, dy, RGB565(r, g, b), a);
            }
        }
        else
        {
            for (sx = 0; sx < info.width; sx++)
            {
                uint8_t b = row[sx * 3 + 0];
                uint8_t g = row[sx * 3 + 1];
                uint8_t r = row[sx * 3 + 2];
                int32_t dx = x + sx;

                if (dx < 0 || dx >= fb->width)
                {
                    continue;
                }
                fb_blend_pixel(fb, dx, dy, RGB565(r, g, b), 255);
            }
        }
    }

    return 0;
}
