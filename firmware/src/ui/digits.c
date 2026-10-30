/*
 * digits.c - the numeric glyphs the UI draws.
 *
 * The glyphs live in the FAT32 image on the external NOR and are drawn
 * straight from it, one row at a time, so the largest of them (nb0.bmp,
 * 82,836 bytes) never has to be resident in RAM.
 */

#include <stdio.h>
#include <string.h>
#include "digits.h"
#include "bmp.h"

typedef struct
{
    const char *prefix;   /* the family prefix, e.g. "setnum" */
    const char *sign;     /* the sign glyph, or NULL */
    const char *dot;      /* the decimal point, or NULL */
} digit_family_t;

static const digit_family_t g_families[DIGIT_STYLE_COUNT] = {
    { "setnum",  0,        "setndot.bmp" },  /* SET   */
    { "mtnum",   "mtnum00.bmp", "mtdot.bmp" },/* MT    */
    { "mtlnum",  0,        0 },              /* MTLN  */
    { "shnum",   0,        0 },              /* SH    */
    { "stnum",   0,        0 },              /* ST    */
    { "tmnum",   0,        0 },              /* TM    */
    { "b",       "b00.bmp", 0 },             /* BIG   */
    { "nb",      0,        0 },              /* NB    */
    { "scb",     0,        0 },              /* SCB   */
    { "shb",     0,        0 },              /* SHB   */
    { "setln",   0,        0 }               /* SETLN */
};

const char *digit_asset_name(digit_style_t style, int digit)
{
    static char name[16];

    if (style >= DIGIT_STYLE_COUNT || digit < 0 || digit > 9)
    {
        return 0;
    }
    snprintf(name, sizeof(name), "%s%d.bmp", g_families[style].prefix, digit);
    return name;
}

const char *digit_sign_asset_name(digit_style_t style)
{
    if (style >= DIGIT_STYLE_COUNT)
    {
        return 0;
    }
    return g_families[style].sign;
}

const char *digit_dot_asset_name(digit_style_t style)
{
    if (style >= DIGIT_STYLE_COUNT)
    {
        return 0;
    }
    return g_families[style].dot;
}

const char *wifi_bar_asset_name(int level)
{
    static char name[16];

    if (level < 0 || level > 4)
    {
        return 0;
    }
    snprintf(name, sizeof(name), "wi%d.bmp", level);
    return name;
}

/* An asset, located in the store, as a streaming BMP source. */
typedef struct
{
    const asset_store_t *store;
    uint32_t             cluster;
} asset_source_t;

static int asset_read_at(void *ctx, uint32_t offset, uint8_t *out, uint32_t len)
{
    asset_source_t *src = (asset_source_t *)ctx;
    int got = asset_store_read(src->store, src->cluster, offset, out, len);

    return (got == (int)len) ? 0 : -1;
}

/* Draws an asset by name, streaming it out of the store. */
int glyph_blit(framebuffer_t *fb, const asset_store_t *store,
               const char *name, int32_t x, int32_t y)
{
    asset_source_t src;
    uint32_t cluster = 0;
    uint32_t size = 0;

    if (fb == 0 || store == 0 || name == 0)
    {
        return -1;
    }
    if (asset_store_lookup(store, name, &cluster, &size) != 0)
    {
        return -1;
    }
    if (size == 0)
    {
        return -1;
    }

    src.store = store;
    src.cluster = cluster;
    return bmp_draw_stream(asset_read_at, &src, size, fb, x, y);
}

int digit_blit(framebuffer_t *fb, const asset_store_t *store,
               digit_style_t style, int digit, int32_t x, int32_t y)
{
    const char *name = digit_asset_name(style, digit);

    if (name == 0)
    {
        return -1;
    }
    return glyph_blit(fb, store, name, x, y);
}

int digit_sign_blit(framebuffer_t *fb, const asset_store_t *store,
                    digit_style_t style, int32_t x, int32_t y)
{
    const char *name = digit_sign_asset_name(style);

    if (name == 0)
    {
        return -1;
    }
    return glyph_blit(fb, store, name, x, y);
}

int digit_dot_blit(framebuffer_t *fb, const asset_store_t *store,
                   digit_style_t style, int32_t x, int32_t y)
{
    const char *name = digit_dot_asset_name(style);

    if (name == 0)
    {
        return -1;
    }
    return glyph_blit(fb, store, name, x, y);
}

int digit_background_blit(framebuffer_t *fb, const asset_store_t *store,
                          int32_t x, int32_t y)
{
    return glyph_blit(fb, store, DIGIT_BACKGROUND_ASSET, x, y);
}

int wifi_bar_blit(framebuffer_t *fb, const asset_store_t *store,
                  int level, int32_t x, int32_t y)
{
    const char *name = wifi_bar_asset_name(level);

    if (name == 0)
    {
        return -1;
    }
    return glyph_blit(fb, store, name, x, y);
}

/* Reads the header of a glyph to get its size. */
static int glyph_header(const asset_store_t *store, const char *name,
                        bmp_info_t *info)
{
    asset_source_t src;
    uint32_t cluster = 0;
    uint32_t size = 0;
    uint8_t  header[64];
    uint32_t header_len;

    if (store == 0 || name == 0)
    {
        return -1;
    }
    if (asset_store_lookup(store, name, &cluster, &size) != 0 || size < 54)
    {
        return -1;
    }

    header_len = (size < sizeof(header)) ? size : sizeof(header);
    src.store = store;
    src.cluster = cluster;
    if (asset_read_at(&src, 0, header, header_len) != 0)
    {
        return -1;
    }
    return bmp_parse_header(header, header_len, info);
}

int digit_measure(const asset_store_t *store, digit_style_t style, int digit,
                  int32_t *width, int32_t *height)
{
    bmp_info_t info;
    const char *name = digit_asset_name(style, digit);

    if (glyph_header(store, name, &info) != 0)
    {
        return -1;
    }
    if (width != 0)
    {
        *width = info.width;
    }
    if (height != 0)
    {
        *height = info.height;
    }
    return 0;
}

int32_t number_width(const asset_store_t *store, digit_style_t style, int digits)
{
    int32_t width = 0;
    int32_t one = 0;

    if (digits <= 0)
    {
        return 0;
    }
    if (digit_measure(store, style, 0, &one, 0) != 0)
    {
        return 0;
    }
    width = one * digits;
    return width;
}

/* Draws a signed fixed-point number.
 *
 * The digits are laid out left to right. With DIGIT_ALIGN_RIGHT the
 * rightmost glyph ends at x, which is how the temperature and setpoint
 * readouts sit in their slots; with DIGIT_ALIGN_LEFT the first glyph
 * starts at x.
 */
int number_blit(framebuffer_t *fb, const asset_store_t *store,
                digit_style_t style, int32_t value, int32_t x, int32_t y,
                int decimals, digit_align_t align, int min_digits)
{
    uint32_t magnitude;
    int      negative = 0;
    int      digits = 0;
    int      i;
    int32_t  glyph_w = 0;
    int32_t  dot_w = 0;
    int32_t  sign_w = 0;
    int32_t  pen;
    int32_t  total;

    if (fb == 0 || store == 0 || style >= DIGIT_STYLE_COUNT)
    {
        return -1;
    }
    if (decimals < 0)
    {
        decimals = 0;
    }
    if (decimals > 1)
    {
        return -1;
    }

    if (value < 0)
    {
        negative = 1;
        magnitude = (uint32_t)(-(int64_t)value);
    }
    else
    {
        magnitude = (uint32_t)value;
    }

    /* Split the magnitude into an integer part and one decimal digit. */
    {
        uint32_t whole = magnitude;
        uint32_t frac = 0;

        if (decimals == 1)
        {
            whole = magnitude / 10U;
            frac = magnitude % 10U;
        }
        digits = 0;
        {
            uint32_t t = whole;
            do
            {
                digits++;
                t /= 10U;
            } while (t != 0);
        }
        if (min_digits > digits)
        {
            digits = min_digits;
        }
        if (decimals == 1)
        {
            digits += 1;      /* the fractional digit is drawn too */
        }
        if (negative)
        {
            digits += 1;      /* and the sign */
        }

        if (digit_measure(store, style, 0, &glyph_w, 0) != 0)
        {
            return -1;
        }

        /* The dot and the sign have their own widths. */
        {
            bmp_info_t info;
            if (decimals == 1 && digit_dot_asset_name(style) != 0 &&
                glyph_header(store, digit_dot_asset_name(style), &info) == 0)
            {
                dot_w = info.width;
            }
            if (negative && digit_sign_asset_name(style) != 0 &&
                glyph_header(store, digit_sign_asset_name(style), &info) == 0)
            {
                sign_w = info.width;
            }
        }

        total = glyph_w * (int32_t)digits;
        if (decimals == 1)
        {
            total += dot_w;
        }
        if (negative)
        {
            total += sign_w;
        }

        pen = (align == DIGIT_ALIGN_RIGHT) ? x - total : x;

        /* Sign. */
        if (negative)
        {
            if (digit_sign_blit(fb, store, style, pen, y) != 0)
            {
                return -1;
            }
            pen += sign_w;
        }

        /* Integer part, zero padded to `digits`. */
        {
            int int_digits = digits - (decimals == 1 ? 1 : 0) - (negative ? 1 : 0);
            char buf[16];
            int  n = 0;
            uint32_t t = whole;

            do
            {
                buf[n++] = (char)('0' + (int)(t % 10U));
                t /= 10U;
            } while (t != 0);
            while (n < int_digits && n < (int)sizeof(buf))
            {
                buf[n++] = '0';
            }
            for (i = n - 1; i >= 0; i--)
            {
                int d = buf[i] - '0';
                if (digit_blit(fb, store, style, d, pen, y) != 0)
                {
                    return -1;
                }
                pen += glyph_w;
            }
        }

        /* Decimal point. */
        if (decimals == 1)
        {
            if (digit_dot_blit(fb, store, style, pen, y) != 0)
            {
                return -1;
            }
            pen += dot_w;
            if (digit_blit(fb, store, style, (int)frac, pen, y) != 0)
            {
                return -1;
            }
            pen += glyph_w;
        }
    }

    return 0;
}
