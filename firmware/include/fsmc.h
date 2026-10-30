/*
 * fsmc.h - external NOR flash on the FSMC.
 *
 * The SST39VF1601 sits on the external bus. The original reaches it
 * through the F1 idiom `sub.w r2, r2, #0x60000000` (0x080837C0), so the
 * device window starts at 0x60000000; the identification sequence runs at
 * 0x64000000, which is the same window 64 MiB further on (see
 * docs/hw-map.md).
 */

#ifndef FSMC_H
#define FSMC_H

#include <stdint.h>

#define FSMC_NOR_BASE           0x60000000UL
#define FSMC_NOR_ID_OFFSET      0x04000000UL
#define FSMC_NOR_SIZE_BYTES     (2UL * 1024UL * 1024UL)

/* Configures the external bus for the NOR. */
void fsmc_init(void);

/* Reads the identification window (offset from 0x64000000). */
uint16_t fsmc_read_id(uint32_t offset);

/* Reads the data window (offset from 0x60000000). */
uint8_t fsmc_read_byte(uint32_t offset);
void fsmc_read_block(uint32_t offset, void *buffer, uint32_t length);

#endif /* FSMC_H */
