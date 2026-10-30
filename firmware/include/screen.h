/*
 * screen.h - the screen and widget layer.
 *
 * The firmware's UI is bitmap-driven: every element is an asset out of
 * the NOR image, and the screens are graphs of those elements. The
 * interaction model uses window messages, of which the firmware's own
 * strings name "WM_AT_TCP_SEND_TEXT" and "WM_AT_TCP_CLOSE"
 * (0x0806b0bc and 0x0806b0f0); the screen graph itself is built at run
 * time from the asset names.
 *
 * The rebuild therefore models a screen as a list of widgets, each of
 * which is either a picture, a label, a number or a button, and a
 * button carries the hit rectangle that the touch layer uses.
 *
 * The asset inventory the screens are built from is recovered in
 * docs/assets.md: 486 referenced assets, of which 191 are path-style
 * ("\White\105.bmp", "\Gray\10.bmp", "\fan\20.bmp", "\nfan\1.bmp",
 * "\Heatbar\15.bmp") and 295 are root-level.
 */

#ifndef SCREEN_H
#define SCREEN_H

#include <stdint.h>
#include "asset_store.h"
#include "digits.h"
#include "framebuffer.h"

#define SCREEN_MAX_WIDGETS 64
#define SCREEN_MAX_NAME   32

/* The widget kinds. */
typedef enum
{
    WIDGET_PICTURE = 0,   /* a bitmap */
    WIDGET_LABEL,         /* a text label, drawn with the small fonts */
    WIDGET_NUMBER,        /* a number, drawn with a digit family */
    WIDGET_BUTTON         /* a picture that responds to a touch */
} widget_kind_t;

/* The layout of one widget. */
typedef struct
{
    widget_kind_t kind;
    int32_t x;
    int32_t y;
    int32_t width;        /* the hit width, for buttons */
    int32_t height;       /* the hit height, for buttons */
    const char *asset;    /* the picture's asset name */
    const char *text;     /* the label's text */
    digit_style_t style;  /* the digit family, for numbers */
    int32_t  value;       /* the number's value */
    int      decimals;    /* the number's decimal places */
    int      id;          /* the widget's identity, returned on a touch */
} widget_t;

/* A screen. */
typedef struct
{
    const char *name;
    widget_t    widgets[SCREEN_MAX_WIDGETS];
    uint32_t    widget_count;
} screen_t;

/* The screen manager. */
typedef struct
{
    const asset_store_t *store;
    framebuffer_t       *fb;
    screen_t           *screens;
    uint32_t            screen_count;
    uint32_t            current;
} screen_manager_t;

/* Prepares the manager. */
void screen_init(screen_manager_t *mgr, const asset_store_t *store,
                 framebuffer_t *fb, screen_t *screens, uint32_t screen_count);

/* Appends a widget to a screen. Returns 0 on success, -1 when the
 * screen is full. */
int screen_add_widget(screen_t *screen, const widget_t *widget);

/* Switches to a screen by index. */
void screen_goto(screen_manager_t *mgr, uint32_t index);

/* The current screen. */
screen_t *screen_current(screen_manager_t *mgr);

/* Draws the current screen. Returns 0 on success. */
int screen_draw(screen_manager_t *mgr);

/* Draws one screen. */
int screen_draw_one(screen_manager_t *mgr, const screen_t *screen);

/* Finds the widget a point hits. Returns the widget and its index, or
 * NULL when the point hits nothing. */
const widget_t *screen_hit(const screen_t *screen, int32_t x, int32_t y,
                           uint32_t *index);

#endif /* SCREEN_H */
