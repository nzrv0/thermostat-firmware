/*
 * screen.c - the screen and widget layer.
 *
 * See screen.h for the evidence.
 */

#include <string.h>
#include "screen.h"

void screen_init(screen_manager_t *mgr, const asset_store_t *store,
                 framebuffer_t *fb, screen_t *screens, uint32_t screen_count)
{
    if (mgr == 0)
    {
        return;
    }
    mgr->store = store;
    mgr->fb = fb;
    mgr->screens = screens;
    mgr->screen_count = screen_count;
    mgr->current = 0;
}

int screen_add_widget(screen_t *screen, const widget_t *widget)
{
    if (screen == 0 || widget == 0)
    {
        return -1;
    }
    if (screen->widget_count >= SCREEN_MAX_WIDGETS)
    {
        return -1;
    }
    screen->widgets[screen->widget_count++] = *widget;
    return 0;
}

void screen_goto(screen_manager_t *mgr, uint32_t index)
{
    if (mgr == 0 || index >= mgr->screen_count)
    {
        return;
    }
    mgr->current = index;
}

screen_t *screen_current(screen_manager_t *mgr)
{
    if (mgr == 0 || mgr->screen_count == 0)
    {
        return 0;
    }
    if (mgr->current >= mgr->screen_count)
    {
        mgr->current = 0;
    }
    return &mgr->screens[mgr->current];
}

int screen_draw_one(screen_manager_t *mgr, const screen_t *screen)
{
    uint32_t i;

    if (mgr == 0 || mgr->fb == 0 || screen == 0)
    {
        return -1;
    }

    for (i = 0; i < screen->widget_count; i++)
    {
        const widget_t *w = &screen->widgets[i];

        switch (w->kind)
        {
        case WIDGET_PICTURE:
        case WIDGET_BUTTON:
            if (w->asset != 0)
            {
                glyph_blit(mgr->fb, mgr->store, w->asset, w->x, w->y);
            }
            break;

        case WIDGET_LABEL:
            /* The label fonts are the small bitmaps; the text itself
             * is drawn by the caller that knows which font to use, so
             * only the asset is drawn here. */
            if (w->asset != 0)
            {
                glyph_blit(mgr->fb, mgr->store, w->asset, w->x, w->y);
            }
            break;

        case WIDGET_NUMBER:
            number_blit(mgr->fb, mgr->store, w->style, w->value, w->x, w->y,
                        w->decimals, DIGIT_ALIGN_LEFT, 1);
            break;

        default:
            break;
        }
    }
    return 0;
}

int screen_draw(screen_manager_t *mgr)
{
    return screen_draw_one(mgr, screen_current(mgr));
}

const widget_t *screen_hit(const screen_t *screen, int32_t x, int32_t y,
                           uint32_t *index)
{
    uint32_t i;

    if (screen == 0)
    {
        return 0;
    }
    for (i = 0; i < screen->widget_count; i++)
    {
        const widget_t *w = &screen->widgets[i];

        if (w->kind != WIDGET_BUTTON)
        {
            continue;
        }
        if (x >= w->x && x < w->x + w->width &&
            y >= w->y && y < w->y + w->height)
        {
            if (index != 0)
            {
                *index = i;
            }
            return w;
        }
    }
    return 0;
}
