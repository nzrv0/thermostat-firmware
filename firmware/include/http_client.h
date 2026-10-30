/*
 * http_client.h - the firmware's HTTP request format.
 *
 * The image builds one request shape: a form-encoded POST with the
 * headers `Content-Type: application/x-www-form-urlencoded`,
 * `Host: %s`, `Content-Length: %d` and `Connection: close`, and a path
 * family under `/HEader/SerialNumber/RoomDevices/...`
 * (0x0807E6AB and 0x0807E6E4).
 */

#ifndef HTTP_CLIENT_H
#define HTTP_CLIENT_H

#include <stddef.h>
#include <stdint.h>

#define HTTP_PATH_MAX      96
#define HTTP_HOST_MAX      64
#define HTTP_BODY_MAX      256

typedef struct
{
    char method[8];
    char path[HTTP_PATH_MAX];
    char host[HTTP_HOST_MAX];
    char content_type[48];
    char body[HTTP_BODY_MAX];
    int  body_length;
} http_request_t;

/* Builds the firmware's request: a POST with the given path and a
 * form-encoded body. */
void http_build_post(http_request_t *request, const char *host,
                     const char *path, const char *body);

/* Appends one "key=value" pair to a form body, percent-encoding the
 * value. Returns the new length. */
int http_form_add(char *body, int length, int capacity, const char *key,
                  const char *value);

/* Renders the request as the bytes that go on the wire. */
int http_render(const http_request_t *request, char *out, int capacity);

#endif /* HTTP_CLIENT_H */
