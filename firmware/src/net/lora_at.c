/*
 * lora_at.c - Dragino style LoRaWAN modem command set.
 */

#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include "lora_at.h"

static const char *after_prefix(const char *line, const char *prefix)
{
    size_t n = strlen(prefix);

    if (strncmp(line, prefix, n) != 0)
    {
        return NULL;
    }
    return line + n;
}

static int copy_field(char *dst, size_t size, const char *src, size_t max)
{
    size_t i = 0;

    while (src[i] && src[i] != ',' && src[i] != '"' && src[i] != '\r' &&
           src[i] != '\n' && i < max && i + 1 < size)
    {
        dst[i] = src[i];
        i++;
    }
    dst[i] = '\0';
    return (int)i;
}

int lora_parse_config(const char *line, lora_config_t *config)
{
    const char *p;

    if (config == 0 || line == 0)
    {
        return -1;
    }
    memset(config, 0, sizeof(*config));
    config->sf = 7;
    config->bw = 125000;
    config->power_dbm = 14;

    p = after_prefix(line, "+CFREQ=");
    if (p != 0)
    {
        config->sf = (uint8_t)atoi(p);
        p = strchr(p, ',');
        if (p != 0)
        {
            config->bw = (uint32_t)strtoul(p + 1, NULL, 10);
        }
    }
    p = after_prefix(line, "+CTXP=");
    if (p != 0)
    {
        config->power_dbm = (int8_t)atoi(p);
    }
    p = after_prefix(line, "+CADR=");
    if (p != 0)
    {
        config->adr = (uint8_t)atoi(p);
    }
    p = after_prefix(line, "+CCLASS=");
    if (p != 0)
    {
        config->cls = (lora_class_t)atoi(p);
    }
    return 0;
}

int lora_parse_identity(const char *line, lora_identity_t *identity)
{
    const char *p;

    if (identity == 0 || line == 0)
    {
        return -1;
    }
    memset(identity, 0, sizeof(*identity));

    p = after_prefix(line, "+CDEVEUI=");
    if (p != 0)
    {
        copy_field(identity->dev_eui, sizeof(identity->dev_eui), p, 16);
        return 0;
    }
    p = after_prefix(line, "+CAPPEUI=");
    if (p != 0)
    {
        copy_field(identity->app_eui, sizeof(identity->app_eui), p, 16);
        return 0;
    }
    p = after_prefix(line, "+CAPPKEY=");
    if (p != 0)
    {
        copy_field(identity->app_key, sizeof(identity->app_key), p, 32);
        return 0;
    }
    return -1;
}

int lora_cmd_set_dev_eui(char *out, size_t size, const char *eui)
{
    int n = snprintf(out, size, "AT+CDEVEUI=%s", eui);

    return (n > 0 && (size_t)n < size) ? 0 : -1;
}

int lora_cmd_set_app_eui(char *out, size_t size, const char *eui)
{
    int n = snprintf(out, size, "AT+CAPPEUI=%s", eui);

    return (n > 0 && (size_t)n < size) ? 0 : -1;
}

int lora_cmd_set_app_key(char *out, size_t size, const char *key)
{
    int n = snprintf(out, size, "AT+CAPPKEY=%s", key);

    return (n > 0 && (size_t)n < size) ? 0 : -1;
}

int lora_cmd_join(char *out, size_t size, int mode, int retry, int period)
{
    int n = snprintf(out, size, "AT+CJOIN=%d,%d,%d", mode, retry, period);

    return (n > 0 && (size_t)n < size) ? 0 : -1;
}

int lora_cmd_class(char *out, size_t size, lora_class_t cls)
{
    int n = snprintf(out, size, "AT+CCLASS=%d", (int)cls);

    return (n > 0 && (size_t)n < size) ? 0 : -1;
}

int lora_cmd_adr(char *out, size_t size, int on)
{
    int n = snprintf(out, size, "AT+CADR=%d", on ? 1 : 0);

    return (n > 0 && (size_t)n < size) ? 0 : -1;
}

int lora_cmd_sf(char *out, size_t size, uint8_t sf)
{
    int n = snprintf(out, size, "AT+CFREQ=%u,125000", (unsigned)sf);

    return (n > 0 && (size_t)n < size) ? 0 : -1;
}

int lora_cmd_bw(char *out, size_t size, uint32_t bw)
{
    int n = snprintf(out, size, "AT+CFREQ=7,%lu", (unsigned long)bw);

    return (n > 0 && (size_t)n < size) ? 0 : -1;
}

int lora_cmd_power(char *out, size_t size, int dbm)
{
    int n = snprintf(out, size, "AT+CTXP=%d", dbm);

    return (n > 0 && (size_t)n < size) ? 0 : -1;
}

int lora_cmd_tx(char *out, size_t size, int confirm)
{
    int n = snprintf(out, size, "AT+CTXP=%d", confirm ? 1 : 0);

    return (n > 0 && (size_t)n < size) ? 0 : -1;
}
