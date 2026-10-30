/*
 * bmp.c - Windows BMP decoder.
 *
 * See bmp.h for the asset format description.
 */

#include "bmp.h"
#include <string.h>

#define BMP_FILE_HEADER_SIZE 14
#define BMP_INFO_HEADER_SIZE 40
#define BMP_HEADER_BYTES     64

/* The row buffer used by the streaming blitter. 480 pixels at 32bpp is
 * 1920 bytes, which is the widest row any panel-sized asset can have;
 * wider rows are processed in several chunks. */
#define BMP_ROW_BUFFER 2048

static uint32_t rd32(const uint8_t *p)
{
    return (uint32_t)p[0] | ((uint32_t)p[1] << 8) |
           ((uint32_t)p[2] << 16) | ((uint32_t)p[3] << 24);
}

static uint16_t rd16(const uint8_t *p)
{
    return (uint16_t)((uint32_t)p[0] | ((uint32_t)p[1] << 8));
}

int bmp_parse_header(const uint8_t *data, uint32_t len, bmp_info_t *out)
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
    if (bpp != 16 && bpp != 24 && bpp != 32)
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
    else if (bpp == 16)
    {
        /* 16bpp rows are padded to a 4-byte boundary. */
        out->row_stride = (((uint32_t)width * 2U) + 3U) & ~3U;
    }
    else
    {
        /* 24bpp rows are padded to a 4-byte boundary. */
        out->row_stride = (((uint32_t)width * 3U) + 3U) & ~3U;
    }

    return 0;
}

int bmp_parse(const uint8_t *data, uint32_t len, bmp_info_t *out)
{
    int rc = bmp_parse_header(data, len, out);

    if (rc != 0)
    {
        return rc;
    }
    if ((uint64_t)out->data_offset +
        (uint64_t)out->row_stride * (uint64_t)out->height > (uint64_t)len)
    {
        return -5;
    }
    return 0;
}

/* Draw one row of pixels. `first` and `count` select a pixel range of
 * the row, so a wide row can be processed in several chunks. */
static void bmp_blit_row(const bmp_info_t *info, const uint8_t *row,
                         framebuffer_t *fb, int32_t x, int32_t dy,
                         int32_t first, int32_t count)
{
    int32_t i;

    if (dy < 0 || dy >= fb->height)
    {
        return;
    }

    if (info->bpp == 16)
    {
        /* The packed NOR image stores the assets as RGB565. */
        for (i = 0; i < count; i++)
        {
            uint16_t value = rd16(row + (first + i) * 2);
            uint8_t r = (uint8_t)((value >> 11) & 0x1F);
            uint8_t g = (uint8_t)((value >> 5) & 0x3F);
            uint8_t b = (uint8_t)(value & 0x1F);
            int32_t dx = x + first + i;

            if (dx < 0 || dx >= fb->width)
            {
                continue;
            }
            fb_blend_pixel(fb, dx, dy,
                           (uint16_t)((r << 11) | (g << 5) | b), 255);
        }
    }
    else if (info->bpp == 32)
    {
        for (i = 0; i < count; i++)
        {
            const uint8_t *p = row + (first + i) * 4;
            uint8_t b = p[0];
            uint8_t g = p[1];
            uint8_t r = p[2];
            uint8_t a = p[3];
            int32_t dx = x + first + i;

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
        for (i = 0; i < count; i++)
        {
            const uint8_t *p = row + (first + i) * 3;
            uint8_t b = p[0];
            uint8_t g = p[1];
            uint8_t r = p[2];
            int32_t dx = x + first + i;

            if (dx < 0 || dx >= fb->width)
            {
                continue;
            }
            fb_blend_pixel(fb, dx, dy, RGB565(r, g, b), 255);
        }
    }
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
        bmp_blit_row(&info, row, fb, x, y + sy, 0, info.width);
    }

    return 0;
}

int bmp_draw_stream(bmp_read_fn read, void *ctx, uint32_t size,
                    framebuffer_t *fb, int32_t x, int32_t y)
{
    static uint8_t header[BMP_HEADER_BYTES];
    static uint8_t rowbuf[BMP_ROW_BUFFER];
    bmp_info_t info;
    uint32_t header_len;
    uint32_t bytes_per_pixel;
    uint32_t pixels_per_chunk;
    int32_t  sy;
    int rc;

    if (read == NULL || fb == NULL)
    {
        return -1;
    }
    /* The smallest asset in the set is a 2x2 decimal point, 62 bytes
     * as a 16bpp BMP, so the header read has to be sized to the file
     * and not to a fixed 64 bytes. */
    header_len = (size < BMP_HEADER_BYTES) ? size : BMP_HEADER_BYTES;
    if (header_len < BMP_FILE_HEADER_SIZE + BMP_INFO_HEADER_SIZE)
    {
        return -1;
    }
    if (read(ctx, 0, header, header_len) != 0)
    {
        return -1;
    }

    rc = bmp_parse_header(header, header_len, &info);
    if (rc != 0)
    {
        return rc;
    }
    if ((uint64_t)info.data_offset +
        (uint64_t)info.row_stride * (uint64_t)info.height > (uint64_t)size)
    {
        return -5;
    }

    bytes_per_pixel = info.bpp / 8U;
    pixels_per_chunk = BMP_ROW_BUFFER / bytes_per_pixel;
    if (pixels_per_chunk == 0)
    {
        return -1;
    }

    for (sy = 0; sy < info.height; sy++)
    {
        uint32_t row_offset = info.data_offset +
                              (uint32_t)(info.height - 1 - sy) * info.row_stride;
        int32_t  done = 0;

        while (done < info.width)
        {
            uint32_t n = (uint32_t)(info.width - done);
            uint32_t chunk_bytes;

            if (n > pixels_per_chunk)
            {
                n = pixels_per_chunk;
            }
            chunk_bytes = n * bytes_per_pixel;

            if (read(ctx, row_offset + (uint32_t)done * bytes_per_pixel,
                     rowbuf, chunk_bytes) != 0)
            {
                return -2;
            }
            bmp_blit_row(&info, rowbuf, fb, x, y + sy, done, (int32_t)n);
            done += (int32_t)n;
        }
    }

    return 0;
}
