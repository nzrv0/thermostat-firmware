/*
 * digits.h - the numeric glyphs the UI draws.
 *
 * The icon set carries the digits as separate bitmaps. The firmware
 * references ten numeric families (recovered from the image; see
 * docs/assets.md and the asset index at 0x080897c0):
 *
 *   setnum0..9   setpoint number          22x33   24bpp/32bpp
 *   mtnum0..9    main temperature number  40x62
 *   mtlnum0..9   main temperature line    16x23
 *   shnum0..9    schedule hour
 *   stnum0..9    status number
 *   tmnum0..9    timer number
 *   b0..9        big number               26x62
 *   nb0..9       number bar              117x177
 *   scb0..9      schedule column bar       22x33
 *   shb0..9      schedule hour bar         22x33
 *   setln0..9    setpoint line number     11x17
 *
 * Three families also have a sign glyph and a decimal point:
 *
 *   mtnum00.bmp  a horizontal bar, the sign in the main-temperature slot
 *   b00.bmp      the same bar for the big-number slot
 *   mtdot.bmp    the decimal point (4x4) of the mt number
 *   setndot.bmp  the decimal point (2x2) of the setpoint number
 *
 * The wifi signal bars are a separate small family, wi0..wi4.
 *
 * The glyphs are drawn straight out of the FAT32 image on the external
 * NOR, one row at a time: nb0.bmp is 82,836 bytes and the device only
 * has 96 KiB of SRAM, so the whole image can never be resident.
 */

#ifndef DIGITS_H
#define DIGITS_H

#include <stdint.h>
#include "asset_store.h"
#include "framebuffer.h"

typedef enum
{
    DIGIT_STYLE_SET = 0,   /* setnum  */
    DIGIT_STYLE_MT,        /* mtnum   */
    DIGIT_STYLE_MTLN,      /* mtlnum  */
    DIGIT_STYLE_SH,        /* shnum   */
    DIGIT_STYLE_ST,        /* stnum   */
    DIGIT_STYLE_TM,        /* tmnum   */
    DIGIT_STYLE_BIG,       /* b       */
    DIGIT_STYLE_NB,        /* nb      */
    DIGIT_STYLE_SCB,       /* scb     */
    DIGIT_STYLE_SHB,       /* shb     */
    DIGIT_STYLE_SETLN,     /* setln   */
    DIGIT_STYLE_COUNT
} digit_style_t;

typedef enum
{
    DIGIT_ALIGN_LEFT = 0,
    DIGIT_ALIGN_RIGHT
} digit_align_t;

/* The asset name for a style and digit, e.g. "setnum3.bmp". */
const char *digit_asset_name(digit_style_t style, int digit);

/* The sign glyph of a style ("mtnum00.bmp"), or NULL if it has none. */
const char *digit_sign_asset_name(digit_style_t style);

/* The decimal point of a style ("setndot.bmp"), or NULL if it has none. */
const char *digit_dot_asset_name(digit_style_t style);

/* The wifi bar asset for a signal level 0..4 ("wi3.bmp"). */
const char *wifi_bar_asset_name(int level);

/* The digit background, digitbg.bmp (304x177). */
#define DIGIT_BACKGROUND_ASSET "digitbg.bmp"

/* Draws a digit. Returns 0 on success. */
int digit_blit(framebuffer_t *fb, const asset_store_t *store,
               digit_style_t style, int digit, int32_t x, int32_t y);

/* Draws the sign glyph of a style. */
int digit_sign_blit(framebuffer_t *fb, const asset_store_t *store,
                    digit_style_t style, int32_t x, int32_t y);

/* Draws the decimal point of a style. */
int digit_dot_blit(framebuffer_t *fb, const asset_store_t *store,
                   digit_style_t style, int32_t x, int32_t y);

/* Draws the digit background. */
int digit_background_blit(framebuffer_t *fb, const asset_store_t *store,
                          int32_t x, int32_t y);

/* Draws a wifi signal bar, level 0..4. */
int wifi_bar_blit(framebuffer_t *fb, const asset_store_t *store,
                  int level, int32_t x, int32_t y);

/* Draws any asset by name. */
int glyph_blit(framebuffer_t *fb, const asset_store_t *store,
               const char *name, int32_t x, int32_t y);

/* Measures a glyph. Returns 0 on success. */
int digit_measure(const asset_store_t *store, digit_style_t style, int digit,
                  int32_t *width, int32_t *height);

/* Draws a signed fixed-point number.
 *
 *   value      the number, in units of 10^-decimals
 *   decimals   0 or 1 (one decimal place, with the decimal point glyph)
 *   align      DIGIT_ALIGN_LEFT or DIGIT_ALIGN_RIGHT, relative to x
 *   min_digits minimum number of digits, zero padded
 *
 * Returns 0 on success, -1 if the style has no digits.
 */
int number_blit(framebuffer_t *fb, const asset_store_t *store,
                digit_style_t style, int32_t value, int32_t x, int32_t y,
                int decimals, digit_align_t align, int min_digits);

/* The width a number of `digits` glyphs occupies, in pixels. */
int32_t number_width(const asset_store_t *store, digit_style_t style,
                     int digits);

#endif /* DIGITS_H */
