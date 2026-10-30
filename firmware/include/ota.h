/*
 * ota.h - the over-the-air update client.
 *
 * What the firmware does, recovered from the image:
 *
 *   - The update files live on an FTP server under
 *     /ftp/Products/MC6-U-Lora/FalconHP_Mark/ (0x0808aac3).
 *   - The file names carry the version in the form U-LW-%d.%02d, so
 *     "U-LW-1.05" is version 1.05.
 *   - The client reports its progress and applies the new image when
 *     the download completes.
 *
 * The rebuild keeps the same shape: the transport (FTP over the ESP-07S
 * AT link, or any other downloader) is a callback, and this module
 * owns the file naming, the version comparison, the progress reporting
 * and the hand-over to the flash writer.
 */

#ifndef OTA_H
#define OTA_H

#include <stdint.h>

#define OTA_PATH      "/ftp/Products/MC6-U-Lora/FalconHP_Mark/"
#define OTA_VERSION_FORMAT "U-LW-%d.%02d"

/* The transport the client drives. */
typedef struct
{
    /* Opens a file on the server. Returns 0 on success. */
    int  (*open)(void *ctx, const char *path, long *size);
    /* Reads from the open file. Returns the number of bytes read, or a
     * negative value on error. */
    int  (*read)(void *ctx, void *buffer, uint32_t len);
    /* Closes the file. */
    void (*close)(void *ctx);
    /* Writes a block to the flash. Returns 0 on success. */
    int  (*write)(void *ctx, uint32_t offset, const uint8_t *data,
                  uint32_t len);
    /* Reports progress. `percent` is 0..100. */
    void (*progress)(void *ctx, int percent);
} ota_transport_t;

/* The client's state. */
typedef struct
{
    const ota_transport_t *transport;
    void                 *ctx;

    int32_t  current_major;
    int32_t  current_minor;
    int32_t  remote_major;
    int32_t  remote_minor;
    long     remote_size;

    uint8_t  buffer[512];
    uint32_t written;
    int      progress;
    int      applied;
} ota_client_t;

/* Prepares the client. */
void ota_init(ota_client_t *client, const ota_transport_t *transport,
               void *ctx);

/* The file name of a version, e.g. "U-LW-1.05". */
int ota_version_filename(char *out, uint32_t size, int32_t major,
                         int32_t minor);

/* Parses "U-LW-%d.%02d" into its two numbers. Returns 0 on success. */
int ota_parse_version(const char *name, int32_t *major, int32_t *minor);

/* Compares two versions. Returns <0, 0 or >0 like strcmp. */
int ota_compare_version(int32_t major_a, int32_t minor_a,
                        int32_t major_b, int32_t minor_b);

/* Checks the server for a newer version.
 *
 * The transport's open() is called with the directory and the file name
 * of the version after the current one. Returns 1 when a newer version
 * exists, 0 when there is none, or -1 on a transport error. */
int ota_check(ota_client_t *client, int32_t major, int32_t minor);

/* Downloads and applies the update that ota_check() found.
 *
 * Returns 0 on success, -1 on a transport error. The progress
 * callback is called as the image is written. */
int ota_apply(ota_client_t *client);

#endif /* OTA_H */
