/*
 * esp_at.h - ESP-07S command set.
 *
 * The Espressif AT commands the firmware uses are listed in
 * docs/subsystems.md. This header names the ones the application needs
 * and the formats of their answers.
 */

#ifndef ESP_AT_H
#define ESP_AT_H

#include <stdint.h>

typedef enum
{
    ESP_WIFI_ST_DISCONNECTED = 0,
    ESP_WIFI_ST_CONNECTED = 1,
    ESP_WIFI_ST_GOT_IP = 2
} esp_wifi_status_t;

/* "AT+CWJAP?" answers "+CWJAP:<ssid>,<rssi>" or
 * "+WIFI:CONNECTED" / "+WIFI:GET_IP_OK" / "+WIFI:GOT_IP". */
typedef struct
{
    char     ssid[33];
    int      rssi;
    esp_wifi_status_t status;
} esp_wifi_info_t;

/* "AT+CIFSR" answers "+CIFSR:<ip>" */
typedef struct
{
    char ip[16];
} esp_ip_info_t;

/* "AT+GMR" answers "+GMR:<version>" */
typedef struct
{
    char version[24];
} esp_version_t;

int esp_parse_wifi(const char *line, esp_wifi_info_t *info);
int esp_parse_ip(const char *line, esp_ip_info_t *info);
int esp_parse_version(const char *line, esp_version_t *info);

/* Builds the command strings the firmware sends. */
int esp_cmd_join(char *out, size_t size, const char *ssid, const char *password);
int esp_cmd_ble_init(char *out, size_t size, int on);
int esp_cmd_blufi(char *out, size_t size, int on);
int esp_cmd_tcp_start(char *out, size_t size, const char *host, int port);
int esp_cmd_tcp_send(char *out, size_t size, int link, int length);
int esp_cmd_close(char *out, size_t size);

#endif /* ESP_AT_H */
