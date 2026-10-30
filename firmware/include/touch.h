/*
 * touch.h - touch handling and the touchpad lock.
 *
 * The firmware publishes a setting "Flag TouchPad Lock"
 * ("FlagiSTouchPadLock", 0x08092ce4, with the label "Flag TouchPad
 * Lock" at 0x08092cd0 and the handler at 0x0803ebc5). It sits in the
 * point registry between "HeatingStatus" and the holiday and holding
 * flags, so it is a published flag that the protocol servers can read
 * and write.
 *
 * No touch controller is identified in the image: there is no touch
 * controller name string, and the firmware drives no I2C peripheral at
 * all, so a capacitive controller would have to be bit-banged. The
 * rebuild therefore takes the touch input from the caller, which is
 * where the panel's controller driver plugs in, and applies the lock
 * behaviour here.
 */

#ifndef TOUCH_H
#define TOUCH_H

#include <stdint.h>
#include "screen.h"

typedef enum
{
    TOUCH_DOWN = 0,
    TOUCH_UP,
    TOUCH_MOVE
} touch_event_t;

typedef struct
{
    touch_event_t event;
    int32_t x;
    int32_t y;
} touch_point_t;

typedef struct
{
    int      locked;        /* the touchpad lock is engaged */
    int      last_widget;   /* the widget the last press hit */
    uint32_t press_count;
} touch_state_t;

/* Prepares the touch state. */
void touch_init(touch_state_t *touch);

/* Engages or releases the touchpad lock. */
void touch_set_locked(touch_state_t *touch, int locked);

/* Is the touchpad locked? */
int touch_is_locked(const touch_state_t *touch);

/* Feeds one touch event to the screen.
 *
 * Returns the identity of the widget that was touched, or -1 when the
 * event did not hit anything. When the touchpad is locked the event is
 * swallowed and -1 is returned, except for a press on the widget the
 * lock itself belongs to, which the caller identifies with
 * `unlock_widget`. */
int touch_handle(touch_state_t *touch, screen_t *screen,
                 const touch_point_t *point, int unlock_widget);

#endif /* TOUCH_H */
