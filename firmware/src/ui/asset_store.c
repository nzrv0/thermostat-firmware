/*
 * asset_store.c - FAT32 read path for the external NOR image store.
 */

#include "asset_store.h"
#include <string.h>

#define ATTR_DIRECTORY 0x10
#define ATTR_LFN       0x0F

static uint32_t rd32(const uint8_t *p)
{
    return (uint32_t)p[0] | ((uint32_t)p[1] << 8) |
           ((uint32_t)p[2] << 16) | ((uint32_t)p[3] << 24);
}

static uint16_t rd16(const uint8_t *p)
{
    return (uint16_t)((uint32_t)p[0] | ((uint32_t)p[1] << 8));
}

/* Sector address of a cluster's first sector. */
static uint32_t cluster_first_sector(const asset_store_t *st, uint32_t cluster)
{
    return st->first_data_sector +
           (cluster - 2U) * st->sectors_per_cluster;
}

static uint32_t fat_next(const asset_store_t *st, uint32_t cluster)
{
    uint8_t  buf[4];
    uint32_t fat_off;
    uint32_t next;

    if (cluster < 2 || cluster >= 0x0FFFFFF8UL)
    {
        return 0xFFFFFFFFUL;
    }

    /* The FAT starts at the first reserved sector; entry N lives at
     * byte offset (reserved_sectors * bytes_per_sector) + N * 4. */
    fat_off = st->reserved_sectors * st->bytes_per_sector + cluster * 4U;
    if (st->read(st->ctx, fat_off, buf, 4) != 0)
    {
        return 0xFFFFFFFFUL;
    }
    next = rd32(buf) & 0x0FFFFFFFUL;
    return next;
}

int asset_store_mount(asset_store_t *st, asset_read_fn read, void *ctx)
{
    uint8_t  bs[512];
    uint32_t fat_size;

    if (st == NULL || read == NULL)
    {
        return -1;
    }

    memset(st, 0, sizeof(*st));
    st->read = read;
    st->ctx  = ctx;

    if (read(ctx, 0, bs, sizeof(bs)) != 0)
    {
        return -2;
    }

    /* Jump boot / extended boot signature. */
    if (bs[510] != 0x55 || bs[511] != 0xAA)
    {
        return -3;
    }
    /* "FAT32" file system type at offset 0x36. */
    if (memcmp(bs + 0x36, "FAT32", 5) != 0)
    {
        return -4;
    }

    st->bytes_per_sector    = rd16(bs + 0x0B);
    st->sectors_per_cluster = bs[0x0D];
    st->reserved_sectors    = rd16(bs + 0x0E);
    st->num_fats            = bs[0x10];
    st->root_cluster        = rd32(bs + 0x44);
    fat_size                = rd16(bs + 0x16);
    if (fat_size == 0)
    {
        fat_size = rd32(bs + 0x24);
    }
    st->fat_size_sectors = fat_size;
    st->total_sectors    = rd16(bs + 0x13);
    if (st->total_sectors == 0)
    {
        st->total_sectors = rd32(bs + 0x20);
    }

    if (st->bytes_per_sector == 0 || st->sectors_per_cluster == 0 ||
        st->num_fats == 0 || st->root_cluster < 2)
    {
        return -5;
    }

    st->first_data_sector = st->reserved_sectors +
                            st->num_fats * st->fat_size_sectors;
    /* FAT32 keeps no separate root-directory area: the root directory is
     * cluster 2, which sits in the data area. */
    st->first_root_sector = st->first_data_sector +
                            (st->root_cluster - 2U) * st->sectors_per_cluster;

    /* Volume label sits at offset 0x2B in the boot sector (11 bytes).
     * The firmware's own boot-sector probe reads the label "AT32". */
    memcpy(st->volume_label, bs + 0x2B, 11);
    st->volume_label[11] = '\0';

    return 0;
}

/* Match a directory entry's 8.3 name against a path component.
 *
 * The 8.3 short name occupies 11 bytes: 8 bytes of base name and 3
 * bytes of extension, each space padded. There is no separator between
 * them. Both parts are compared case-insensitively, which matters
 * because the firmware refers to directories as "\White" and "\Gray"
 * while the FAT stores them upper-cased as "WHITE" and "GRAY".
 *
 * A component with no extension (a directory name) matches an entry
 * whose extension field is empty.
 */
static int name_match(const uint8_t *entry_name, const char *want)
{

    char base[9];
    char ext[4];
    const char *dot;

    const char *b;
    int alen;
    int blen;
    int i;

    /* Base name: bytes 0..7, trimmed of trailing spaces. */
    for (i = 0; i < 8 && entry_name[i] != ' '; i++)
    {
        base[i] = (char)entry_name[i];
    }
    base[i] = '\0';
    /* Trim any trailing spaces the loop left behind. */
    while (i > 0 && base[i - 1] == ' ')
    {
        base[--i] = '\0';
    }

    /* Extension: bytes 8..10, trimmed. */
    for (i = 0; i < 3 && entry_name[8 + i] != ' '; i++)
    {
        ext[i] = (char)entry_name[8 + i];
    }
    ext[i] = '\0';

    dot = strrchr(want, '.');
    if (dot != NULL)
    {
        /* "NAME.EXT": the base part of the entry must match the part of
         * the requested name before the dot. */
        alen = (int)(dot - want);
        b    = dot + 1;
        blen = (int)strlen(b);
    }
    else
    {
        /* No extension wanted: the whole component is the base and the
         * entry must have no extension either. */
        alen = (int)strlen(want);
        b    = "";
        blen = 0;
        if (ext[0] != '\0')
        {
            return 0;
        }
    }

    /* Compare the base name against the requested name's base part. */
    if (alen != (int)strlen(base))
    {
        return 0;
    }
    for (i = 0; i < alen; i++)
    {
        char ca = base[i];
        char cb = want[i];

        if (ca >= 'a' && ca <= 'z') { ca = (char)(ca - 'a' + 'A'); }
        if (cb >= 'a' && cb <= 'z') { cb = (char)(cb - 'a' + 'A'); }
        if (ca != cb)
        {
            return 0;
        }
    }

    /* Compare the extension against the requested name's extension. */
    if (blen == 0)
    {
        return 1;
    }
    for (i = 0; i < 3 && ext[i]; i++)
    {
        char ca = ext[i];
        char cb = b[i];

        if (cb == '\0')
        {
            return 0;
        }
        if (ca >= 'a' && ca <= 'z') { ca = (char)(ca - 'a' + 'A'); }
        if (cb >= 'a' && cb <= 'z') { cb = (char)(cb - 'a' + 'A'); }
        if (ca != cb)
        {
            return 0;
        }
    }
    /* The entry's extension must not be longer than the requested one. */
    if (ext[blen] != '\0')
    {
        return 0;
    }

    return 1;
}

/* Search one directory's cluster chain for a name. Returns the file's
 * first cluster and size. */
static int find_in_dir(const asset_store_t *st, uint32_t dir_cluster,
                      const char *name, uint32_t *out_cluster,
                      uint32_t *out_size)
{
    uint8_t  sector[512];
    uint32_t cluster = dir_cluster;
    uint32_t guard = 0;

    while (cluster >= 2 && cluster < 0x0FFFFFF8UL && guard++ < 4096)
    {
        uint32_t sec = cluster_first_sector(st, cluster);
        uint32_t s;

        for (s = 0; s < st->sectors_per_cluster; s++)
        {
            uint32_t off = (sec + s) * st->bytes_per_sector;
            uint32_t e;

            if (st->read(st->ctx, off, sector, st->bytes_per_sector) != 0)
            {
                return -1;
            }

            for (e = 0; e + 32 <= st->bytes_per_sector; e += 32)
            {
                uint8_t attr = sector[e + 11];

                if (sector[e] == 0x00)
                {
                    return -2;      /* end of directory */
                }
                if (sector[e] == 0xE5)
                {
                    continue;        /* deleted */
                }
                if (attr == ATTR_LFN)
                {
                    /* Long file name fragment: skipped, the 8.3 entry
                     * carries the name the firmware looks up. */
                    continue;
                }
                if (attr & ATTR_DIRECTORY)
                {
                    continue;
                }
                if (name_match(sector + e, name))
                {
                    *out_cluster = ((uint32_t)sector[e + 20]) |
                                   ((uint32_t)sector[e + 21] << 8) |
                                   ((uint32_t)sector[e + 26] << 16) |
                                   ((uint32_t)sector[e + 27] << 24);
                    *out_size = (uint32_t)sector[e + 28] |
                                ((uint32_t)sector[e + 29] << 8) |
                                ((uint32_t)sector[e + 30] << 16) |
                                ((uint32_t)sector[e + 31] << 24);
                    return 0;
                }
            }
        }

        cluster = fat_next(st, cluster);
    }

    return -3;
}

/* Find a subdirectory by name; returns its first cluster. */
static int find_subdir(const asset_store_t *st, uint32_t dir_cluster,
                       const char *name, uint32_t *out_cluster)
{
    uint8_t  sector[512];
    uint32_t cluster = dir_cluster;
    uint32_t guard = 0;

    while (cluster >= 2 && cluster < 0x0FFFFFF8UL && guard++ < 4096)
    {
        uint32_t sec = cluster_first_sector(st, cluster);
        uint32_t s;

        for (s = 0; s < st->sectors_per_cluster; s++)
        {
            uint32_t off = (sec + s) * st->bytes_per_sector;
            uint32_t e;

            if (st->read(st->ctx, off, sector, st->bytes_per_sector) != 0)
            {
                return -1;
            }
            for (e = 0; e + 32 <= st->bytes_per_sector; e += 32)
            {
                uint8_t attr = sector[e + 11];

                if (sector[e] == 0x00)
                {
                    return -2;
                }
                if (sector[e] == 0xE5)
                {
                    continue;
                }
                if ((attr & ATTR_DIRECTORY) && name_match(sector + e, name))
                {
                    *out_cluster = ((uint32_t)sector[e + 20]) |
                                   ((uint32_t)sector[e + 21] << 8) |
                                   ((uint32_t)sector[e + 26] << 16) |
                                   ((uint32_t)sector[e + 27] << 24);
                    return 0;
                }
            }
        }
        cluster = fat_next(st, cluster);
    }
    return -3;
}

uint32_t asset_store_find(const asset_store_t *st, const char *path)
{
    char     comp[ASSET_NAME_MAX];
    uint32_t cluster = st->root_cluster;
    uint32_t file_cluster = 0;
    uint32_t size = 0;
    const char *p = path;

    if (st == NULL || path == NULL)
    {
        return ASSET_INVALID;
    }

    /* Skip any leading backslash. */
    while (*p == '\\')
    {
        p++;
    }

    while (*p != '\0')
    {
        const char *slash = strchr(p, '\\');
        size_t      len = slash ? (size_t)(slash - p) : strlen(p);

        if (len == 0 || len >= sizeof(comp))
        {
            return ASSET_INVALID;
        }
        memcpy(comp, p, len);
        comp[len] = '\0';

        if (slash == NULL)
        {
            /* Last component: the file. */
            if (find_in_dir(st, cluster, comp, &file_cluster, &size) != 0)
            {
                return ASSET_INVALID;
            }
            return file_cluster;
        }

        if (find_subdir(st, cluster, comp, &cluster) != 0)
        {
            return ASSET_INVALID;
        }
        p = slash + 1;
    }

    return ASSET_INVALID;
}

uint32_t asset_store_size(const asset_store_t *st, const char *path)
{
    char     comp[ASSET_NAME_MAX];
    uint32_t cluster = st->root_cluster;
    uint32_t file_cluster = 0;
    uint32_t size = 0;
    const char *p = path;

    while (*p == '\\')
    {
        p++;
    }

    while (*p != '\0')
    {
        const char *slash = strchr(p, '\\');
        size_t      len = slash ? (size_t)(slash - p) : strlen(p);

        if (len == 0 || len >= sizeof(comp))
        {
            return 0;
        }
        memcpy(comp, p, len);
        comp[len] = '\0';

        if (slash == NULL)
        {
            if (find_in_dir(st, cluster, comp, &file_cluster, &size) != 0)
            {
                return 0;
            }
            return size;
        }
        if (find_subdir(st, cluster, comp, &cluster) != 0)
        {
            return 0;
        }
        p = slash + 1;
    }
    return 0;
}

int asset_store_read(const asset_store_t *st, uint32_t first_cluster,
                     uint32_t offset, uint8_t *out, uint32_t len)
{
    uint32_t bytes_per_cluster = st->sectors_per_cluster * st->bytes_per_sector;
    uint32_t cluster_index;
    uint32_t cluster;
    uint32_t done = 0;

    if (st == NULL || out == NULL)
    {
        return -1;
    }
    if (first_cluster < 2 || first_cluster >= 0x0FFFFFF8UL)
    {
        return -1;
    }

    /* Skip whole clusters to reach the requested offset. */
    cluster_index = offset / bytes_per_cluster;
    offset       = offset % bytes_per_cluster;

    cluster = first_cluster;
    while (cluster_index-- > 0 && cluster >= 2 && cluster < 0x0FFFFFF8UL)
    {
        cluster = fat_next(st, cluster);
        if (cluster == 0xFFFFFFFFUL)
        {
            return -2;
        }
    }

    while (done < len && cluster >= 2 && cluster < 0x0FFFFFF8UL)
    {
        uint32_t sec = cluster_first_sector(st, cluster);
        uint32_t s;

        for (s = 0; s < st->sectors_per_cluster && done < len; s++)
        {
            uint32_t addr = (sec + s) * st->bytes_per_sector;
            uint32_t chunk = st->bytes_per_sector - offset;

            if (chunk > len - done)
            {
                chunk = len - done;
            }
            if (st->read(st->ctx, addr + offset, out + done, chunk) != 0)
            {
                return -3;
            }
            done += chunk;
            offset = 0;
        }
        cluster = fat_next(st, cluster);
        if (cluster == 0xFFFFFFFFUL)
        {
            break;
        }
    }

    return (int)done;
}
