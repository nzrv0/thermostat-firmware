/*
 * ota.c - the over-the-air update client.
 *
 * See ota.h for the evidence.
 */

#include <stdio.h>
#include <string.h>
#include "ota.h"

void ota_init(ota_client_t *client, const ota_transport_t *transport,
               void *ctx)
{
    if (client == 0)
    {
        return;
    }
    memset(client, 0, sizeof(*client));
    client->transport = transport;
    client->ctx = ctx;
}

int ota_version_filename(char *out, uint32_t size, int32_t major,
                         int32_t minor)
{
    int n;

    if (out == 0 || size == 0)
    {
        return -1;
    }
    n = snprintf(out, size, OTA_VERSION_FORMAT, (int)major, (int)minor);
    if (n < 0 || (uint32_t)n >= size)
    {
        return -1;
    }
    return n;
}

int ota_parse_version(const char *name, int32_t *major, int32_t *minor)
{
    int ma = 0;
    int mi = 0;
    int digits = 0;
    const char *p = name;

    if (name == 0 || major == 0 || minor == 0)
    {
        return -1;
    }
    if (strncmp(p, "U-LW-", 5) != 0)
    {
        return -1;
    }
    p += 5;

    while (*p >= '0' && *p <= '9')
    {
        ma = ma * 10 + (*p - '0');
        p++;
        digits++;
        if (digits > 4)
        {
            return -1;
        }
    }
    if (digits == 0 || *p != '.')
    {
        return -1;
    }
    p++;
    digits = 0;
    while (*p >= '0' && *p <= '9')
    {
        mi = mi * 10 + (*p - '0');
        p++;
        digits++;
        if (digits > 2)
        {
            return -1;
        }
    }
    if (digits == 0)
    {
        return -1;
    }
    *major = ma;
    *minor = mi;
    return 0;
}

int ota_compare_version(int32_t major_a, int32_t minor_a,
                        int32_t major_b, int32_t minor_b)
{
    if (major_a != major_b)
    {
        return (major_a < major_b) ? -1 : 1;
    }
    if (minor_a != minor_b)
    {
        return (minor_a < minor_b) ? -1 : 1;
    }
    return 0;
}

int ota_check(ota_client_t *client, int32_t major, int32_t minor)
{
    char path[128];
    char name[32];
    long size = 0;

    if (client == 0 || client->transport == 0 ||
        client->transport->open == 0)
    {
        return -1;
    }

    client->current_major = major;
    client->current_minor = minor;

    /* The next version is the next minor of the same major, and then
     * the next major. */
    if (ota_version_filename(name, sizeof(name), major, minor + 1) < 0)
    {
        return -1;
    }
    snprintf(path, sizeof(path), "%s%s", OTA_PATH, name);
    if (client->transport->open(client->ctx, path, &size) == 0)
    {
        client->transport->close(client->ctx);
        client->remote_major = major;
        client->remote_minor = minor + 1;
        client->remote_size = size;
        return 1;
    }

    if (ota_version_filename(name, sizeof(name), major + 1, 0) < 0)
    {
        return -1;
    }
    snprintf(path, sizeof(path), "%s%s", OTA_PATH, name);
    if (client->transport->open(client->ctx, path, &size) == 0)
    {
        client->transport->close(client->ctx);
        client->remote_major = major + 1;
        client->remote_minor = 0;
        client->remote_size = size;
        return 1;
    }

    return 0;
}

int ota_apply(ota_client_t *client)
{
    char path[128];
    char name[32];
    long size = 0;
    uint32_t offset = 0;
    int last_percent = -1;

    if (client == 0 || client->transport == 0 ||
        client->transport->open == 0 || client->transport->read == 0 ||
        client->transport->write == 0)
    {
        return -1;
    }
    if (client->remote_major < 0 && client->remote_minor < 0)
    {
        return -1;
    }

    if (ota_version_filename(name, sizeof(name), client->remote_major,
                             client->remote_minor) < 0)
    {
        return -1;
    }
    snprintf(path, sizeof(path), "%s%s", OTA_PATH, name);

    if (client->transport->open(client->ctx, path, &size) != 0)
    {
        return -1;
    }

    for (;;)
    {
        int n = client->transport->read(client->ctx, client->buffer,
                                       sizeof(client->buffer));
        if (n < 0)
        {
            client->transport->close(client->ctx);
            return -1;
        }
        if (n == 0)
        {
            break;
        }
        if (client->transport->write(client->ctx, offset, client->buffer,
                                     (uint32_t)n) != 0)
        {
            client->transport->close(client->ctx);
            return -1;
        }
        offset += (uint32_t)n;
        client->written = offset;

        if (size > 0 && client->transport->progress != 0)
        {
            int percent = (int)((offset * 100UL) / (uint32_t)size);
            if (percent != last_percent)
            {
                last_percent = percent;
                client->transport->progress(client->ctx, percent);
            }
        }
    }

    client->transport->close(client->ctx);
    client->applied = 1;
    client->progress = 100;
    return 0;
}
