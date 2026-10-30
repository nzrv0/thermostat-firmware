/*
 * panel.c - the 480x320 colour display.
 *
 * See panel.h for the evidence.
 */

#include <string.h>
#include "panel.h"

static void bus_write(panel_t *panel, const uint8_t *data, uint32_t len)
{
    if (panel->bus != 0 && panel->bus->write != 0)
    {
        panel->bus->write(panel->ctx, data, len);
    }
}

void panel_command(panel_t *panel, uint8_t command)
{
    panel_command_data(panel, command, 0, 0);
}

void panel_command_data(panel_t *panel, uint8_t command,
                        const uint8_t *data, uint32_t count)
{
    uint8_t buffer[16];
    uint32_t n = 0;

    buffer[n++] = command;
    if (count > 0 && data != 0)
    {
        if (count > sizeof(buffer) - 1)
        {
            count = sizeof(buffer) - 1;
        }
        memcpy(buffer + n, data, count);
        n += count;
    }
    bus_write(panel, buffer, n);
}

void panel_set_window(panel_t *panel, int32_t x, int32_t y,
                      int32_t w, int32_t h)
{
    uint8_t args[4];

    x += panel->xoffset;
    y += panel->yoffset;

    args[0] = (uint8_t)(x >> 8);
    args[1] = (uint8_t)(x & 0xFF);
    args[2] = (uint8_t)((x + w - 1) >> 8);
    args[3] = (uint8_t)((x + w - 1) & 0xFF);
    panel_command_data(panel, DCS_CASET, args, 4);

    args[0] = (uint8_t)(y >> 8);
    args[1] = (uint8_t)(y & 0xFF);
    args[2] = (uint8_t)((y + h - 1) >> 8);
    args[3] = (uint8_t)((y + h - 1) & 0xFF);
    panel_command_data(panel, DCS_RASET, args, 4);
}

void panel_write_pixels(panel_t *panel, const uint8_t *pixels, uint32_t len)
{
    bus_write(panel, pixels, len);
}

void panel_display_on(panel_t *panel, int on)
{
    panel_command(panel, on ? DCS_DISPON : DCS_INVOFF);
}

/* The default power-on sequence, expressed as command groups. */
struct init_group
{
    uint8_t command;
    uint8_t args[4];
    uint8_t count;
};

static const struct init_group g_default_groups[] = {
    { DCS_SWRESET,  { 0x00 }, 0 },
    { DCS_SLPOUT,   { 0x00 }, 0 },
    { DCS_COLMOD,  { 0x55 }, 1 },   /* 16 bits per pixel, RGB565 */
    { DCS_MADCTL,  { 0x00 }, 1 },
    { DCS_DISPON,  { 0x00 }, 0 }
};

static void run_groups(panel_t *panel, const struct init_group *groups,
                       uint32_t count)
{
    uint32_t i;

    for (i = 0; i < count; i++)
    {
        panel_command_data(panel, groups[i].command, groups[i].args,
                           groups[i].count);
    }
}

void panel_init(panel_t *panel, const panel_bus_t *bus, void *ctx)
{
    if (panel == 0)
    {
        return;
    }

    panel->bus = bus;
    panel->ctx = ctx;
    panel->width = PANEL_WIDTH;
    panel->height = PANEL_HEIGHT;
    panel->xoffset = 0;
    panel->yoffset = 0;
    panel->madctl = MADCTL_MY | MADCTL_MV | MADCTL_BGR;
    panel->colmod = 0x55;          /* RGB565 */
    panel->inversion = 0;

    /* Reset the controller. */
    panel_command(panel, DCS_SWRESET);

    /* Sleep out; the controller needs 120 ms before it accepts the
     * display-on command. */
    panel_command(panel, DCS_SLPOUT);
    if (bus != 0 && bus->millis != 0)
    {
        uint32_t start = bus->millis(ctx);

        /* The controller needs 120 ms after sleep-out before it
         * accepts the display-on command. */
        while ((bus->millis(ctx) - start) < 120u)
        {
            /* the time base is the scheduler's */
        }
    }

    /* Pixel format, then orientation. */
    {
        uint8_t arg = panel->colmod;
        panel_command_data(panel, DCS_COLMOD, &arg, 1);
    }
    {
        uint8_t arg = panel->madctl;
        panel_command_data(panel, DCS_MADCTL, &arg, 1);
    }

    if (panel->inversion)
    {
        panel_command(panel, DCS_INVOFF);
    }
    else
    {
        panel_command(panel, DCS_IDMOFF);
    }

    /* An integrator-supplied controller sequence replaces the default
     * one; it is a flat list of command groups in the same format as
     * the table above. */
    if (panel->init_table != 0 && panel->init_length != 0)
    {
        const uint8_t *p = panel->init_table;
        uint32_t left = panel->init_length;

        while (left >= 2)
        {
            uint8_t cmd = p[0];
            uint8_t count = p[1];

            if (count > left - 2)
            {
                count = (uint8_t)(left - 2);
            }
            panel_command_data(panel, cmd, p + 2, count);
            left -= (uint32_t)count + 2;
            p += count + 2;
        }
    }
    else
    {
        run_groups(panel, g_default_groups,
                   sizeof(g_default_groups) / sizeof(g_default_groups[0]));
    }

    panel->initialised = 1;
    panel_display_on(panel, 1);
}
