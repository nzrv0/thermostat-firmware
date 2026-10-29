/*
 * bmp.h - Windows BMP decoder for the UI asset format.
 *
 * The supplied asset set (Mark-icons-2025.10.29) is Windows BMP:
 *
 *   - 404 of 419 files are 32bpp BI_RGB, uncompressed, bottom-up,
 *     with the alpha byte used as a real transparency/opacity channel.
 *   - 12 files (digitbg.bmp and friends) are 24bpp BI_RGB, bottom-up,
 *     with rows padded to a 4-byte boundary and no alpha channel.
 *
 * Both forms are decoded here into the framebuffer's RGB565 format.
 * 24bpp images are treated as fully opaque.
 *
 * A 32bpp pixel is stored as B, G, R, A. The alpha value is used as an
 * opacity: 0 is fully transparent, 255 fully opaque, and intermediate
 * values blend (the set contains anti-aliased edges, so a hard
 * transparency test would leave visible fringes).
 */

#ifndef BMP_H
#define BMP_H

#include <stdint.h>
#include "framebuffer.h"

typedef struct
{
    int32_t      width;
    int32_t      height;      /* positive: rows stored bottom-up */
    uint32_t     bpp;
    uint32_t     data_offset;
    uint32_t     row_stride;  /* bytes per row in the file */
    int          has_alpha;
} bmp_info_t;

/* Parse a BMP header held in memory. Returns 0 on success. */
int bmp_parse(const uint8_t *data, uint32_t len, bmp_info_t *out);

/* Draw a BMP into the framebuffer at (x, y). Pixels outside the
 * framebuffer are clipped. Returns 0 on success. */
int bmp_draw(const uint8_t *data, uint32_t len, framebuffer_t *fb,
             int32_t x, int32_t y);

#endif /* BMP_H */
