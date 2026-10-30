/*
 * http_client.c - the firmware's HTTP request format.
 */

#include <stdio.h>
#include <string.h>
#include "http_client.h"

static int is_unreserved(char c)
{
    return ((c >= 'a' && c <= 'z') || (c >= 'A' && c <= 'Z') ||
            (c >= '0' && c <= '9') || c == '-' || c == '_' || c == '.' ||
            c == '~');
}

static int append(char *out, int length, int capacity, const char *text)
{
    while (*text && length + 1 < capacity)
    {
        out[length++] = *text++;
    }
    if (length < capacity)
    {
        out[length] = '\0';
    }
    return length;
}

void http_build_post(http_request_t *request, const char *host,
                     const char *path, const char *body)
{
    if (request == 0)
    {
        return;
    }

    memset(request, 0, sizeof(*request));
    snprintf(request->method, sizeof(request->method), "POST");
    snprintf(request->path, sizeof(request->path), "%s", path ? path : "/");
    snprintf(request->host, sizeof(request->host), "%s", host ? host : "");
    snprintf(request->content_type, sizeof(request->content_type),
             "application/x-www-form-urlencoded");
    if (body != 0)
    {
        request->body_length = (int)strlen(body);
        if (request->body_length >= HTTP_BODY_MAX)
        {
            request->body_length = HTTP_BODY_MAX - 1;
        }
        memcpy(request->body, body, (size_t)request->body_length);
    }
    request->body[request->body_length] = '\0';
}

int http_form_add(char *body, int length, int capacity, const char *key,
                  const char *value)
{
    if (body == 0 || key == 0)
    {
        return length;
    }

    if (length > 0)
    {
        length = append(body, length, capacity, "&");
    }
    length = append(body, length, capacity, key);
    length = append(body, length, capacity, "=");

    while (value != 0 && *value)
    {
        char c = *value++;

        if (is_unreserved(c))
        {
            length = append(body, length, capacity, (char[]){c, 0});
        }
        else
        {
            char encoded[4];

            snprintf(encoded, sizeof(encoded), "%%%02X", (unsigned char)c);
            length = append(body, length, capacity, encoded);
        }
    }
    return length;
}

int http_render(const http_request_t *request, char *out, int capacity)
{
    int length = 0;

    if (request == 0 || out == 0 || capacity <= 0)
    {
        return 0;
    }

    out[0] = '\0';
    length = append(out, length, capacity, request->method);
    length = append(out, length, capacity, " ");
    length = append(out, length, capacity, request->path);
    length = append(out, length, capacity, " HTTP/1.1\r\n");
    length = append(out, length, capacity, "Host: ");
    length = append(out, length, capacity, request->host);
    length = append(out, length, capacity, "\r\n");
    length = append(out, length, capacity, "Content-Type: ");
    length = append(out, length, capacity, request->content_type);
    length = append(out, length, capacity, "\r\n");
    length = append(out, length, capacity, "Content-Length: ");
    {
        char number[16];

        snprintf(number, sizeof(number), "%d", request->body_length);
        length = append(out, length, capacity, number);
    }
    length = append(out, length, capacity, "\r\n");
    length = append(out, length, capacity, "Connection: close\r\n");
    length = append(out, length, capacity, "\r\n");
    length = append(out, length, capacity, request->body);
    return length;
}
