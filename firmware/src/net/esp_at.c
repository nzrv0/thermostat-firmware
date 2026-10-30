/*
 * esp_at.c - ESP-07S command set.
 */

#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include "esp_at.h"

static int copy_field(char *dst, size_t size, const char *src, size_t max)
{
    size_t i = 0;

    while (src[i] && src[i] != ',' && src[i] != '\r' && src[i] != '\n' &&
           i < max && i + 1 < size)
    {
        dst[i] = src[i];
        i++;
    }
    dst[i] = '\0';
    return (int)i;
}

static const char *after_prefix(const char *line, const char *prefix)
{
    size_t n = strlen(prefix);

    if (strncmp(line, prefix, n) != 0)
    {
        return NULL;
    }
    return line + n;
}

int esp_parse_wifi(const char *line, esp_wifi_info_t *info)
{
    const char *p;

    if (info == 0 || line == 0)
    {
        return -1;
    }
    memset(info, 0, sizeof(*info));

    p = after_prefix(line, "+CWJAP:");
    if (p != 0)
    {
        copy_field(info->ssid, sizeof(info->ssid), p, 32);
        p = strchr(p, ',');
        if (p != 0)
        {
            info->rssi = atoi(p + 1);
        }
        info->status = ESP_WIFI_ST_CONNECTED;
        return 0;
    }
    if (strcmp(line, "+WIFI:CONNECTED") == 0)
    {
        info->status = ESP_WIFI_ST_CONNECTED;
        return 0;
    }
    if (strcmp(line, "+WIFI:GOT_IP") == 0 ||
        strcmp(line, "+WIFI:GET_IP_OK") == 0)
    {
        info->status = ESP_WIFI_ST_GOT_IP;
        return 0;
    }
    return -1;
}

int esp_parse_ip(const char *line, esp_ip_info_t *info)
{
    const char *p;

    if (info == 0 || line == 0)
    {
        return -1;
    }
    memset(info, 0, sizeof(*info));

    p = after_prefix(line, "+CIFSR:");
    if (p == 0)
    {
        return -1;
    }
    copy_field(info->ip, sizeof(info->ip), p, 15);
    return 0;
}

int esp_parse_version(const char *line, esp_version_t *info)
{
    const char *p;

    if (info == 0 || line == 0)
    {
        return -1;
    }
    memset(info, 0, sizeof(*info));

    p = after_prefix(line, "+GMR:");
    if (p == 0)
    {
        return -1;
    }
    copy_field(info->version, sizeof(info->version), p, 23);
    return 0;
}

int esp_cmd_join(char *out, size_t size, const char *ssid, const char *password)
{
    int n = snprintf(out, size, "AT+CWJAP_CUR=\"%s\",\"%s\"", ssid, password);

    return (n > 0 && (size_t)n < size) ? 0 : -1;
}

int esp_cmd_ble_init(char *out, size_t size, int on)
{
    int n = snprintf(out, size, "AT+BLEINIT=%d", on ? 2 : 0);

    return (n > 0 && (size_t)n < size) ? 0 : -1;
}

int esp_cmd_blufi(char *out, size_t size, int on)
{
    int n = snprintf(out, size, "AT+BLUFI=%d", on ? 1 : 0);

    return (n > 0 && (size_t)n < size) ? 0 : -1;
}

int esp_cmd_tcp_start(char *out, size_t size, const char *host, int port)
{
    int n = snprintf(out, size, "AT+CIPSTART=\"TCP\",\"%s\",%d", host, port);

    return (n > 0 && (size_t)n < size) ? 0 : -1;
}

int esp_cmd_tcp_send(char *out, size_t size, int link, int length)
{
    int n = snprintf(out, size, "AT+CIPSEND=%d,%d", link, length);

    return (n > 0 && (size_t)n < size) ? 0 : -1;
}

int esp_cmd_close(char *out, size_t size)
{
    int n = snprintf(out, size, "AT+CIPCLOSE");

    return (n > 0 && (size_t)n < size) ? 0 : -1;
}
