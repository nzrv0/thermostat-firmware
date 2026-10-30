/*
 * touch.c - touch handling and the touchpad lock.
 *
 * See touch.h for the evidence.
 */

#include <string.h>
#include "touch.h"

void touch_init(touch_state_t *touch)
{
    if (touch == 0)
    {
        return;
    }
    memset(touch, 0, sizeof(*touch));
    touch->last_widget = -1;
}

void touch_set_locked(touch_state_t *touch, int locked)
{
    if (touch == 0)
    {
        return;
    }
    touch->locked = locked ? 1 : 0;
}

int touch_is_locked(const touch_state_t *touch)
{
    if (touch == 0)
    {
        return 0;
    }
    return touch->locked;
}

int touch_handle(touch_state_t *touch, screen_t *screen,
                 const touch_point_t *point, int unlock_widget)
{
    const widget_t *hit;
    uint32_t index = 0;

    if (touch == 0 || screen == 0 || point == 0)
    {
        return -1;
    }

    hit = screen_hit(screen, point->x, point->y, &index);
    if (hit == 0)
    {
        touch->last_widget = -1;
        return -1;
    }

    /* The lock swallows every touch except the one that unlocks it. */
    if (touch->locked && hit->id != unlock_widget)
    {
        touch->last_widget = -1;
        return -1;
    }

    if (point->event == TOUCH_DOWN)
    {
        touch->press_count++;
        touch->last_widget = hit->id;
        return hit->id;
    }

    /* A move or a release does not activate anything; it only updates
     * the last position. */
    return -1;
}
