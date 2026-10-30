/*
 * lora_at.h - Dragino style LoRaWAN modem command set.
 *
 * The commands the firmware uses are listed in docs/subsystems.md.
 */

#ifndef LORA_AT_H
#define LORA_AT_H

#include <stdint.h>

typedef enum
{
    LORA_CLASS_A = 0,
    LORA_CLASS_C = 2
} lora_class_t;

typedef struct
{
    uint8_t sf;          /* spreading factor, 7..12 */
    uint32_t bw;         /* bandwidth, 125000 */
    int8_t  power_dbm;   /* transmit power, 3..17 */
    uint8_t  adr;
    lora_class_t cls;
} lora_config_t;

typedef struct
{
    char dev_eui[17];
    char app_eui[17];
    char app_key[33];
} lora_identity_t;

int lora_parse_config(const char *line, lora_config_t *config);
int lora_parse_identity(const char *line, lora_identity_t *identity);

int lora_cmd_set_dev_eui(char *out, size_t size, const char *eui);
int lora_cmd_set_app_eui(char *out, size_t size, const char *eui);
int lora_cmd_set_app_key(char *out, size_t size, const char *key);
int lora_cmd_join(char *out, size_t size, int mode, int retry, int period);
int lora_cmd_class(char *out, size_t size, lora_class_t cls);
int lora_cmd_adr(char *out, size_t size, int on);
int lora_cmd_sf(char *out, size_t size, uint8_t sf);
int lora_cmd_bw(char *out, size_t size, uint32_t bw);
int lora_cmd_power(char *out, size_t size, int dbm);
int lora_cmd_tx(char *out, size_t size, int confirm);

#endif /* LORA_AT_H */
