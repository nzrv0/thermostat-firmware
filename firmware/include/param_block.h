/*
 * param_block.h - the shared parameter block at flash 0x08005000.
 *
 * The shipped firmware reads and writes a structure at 0x08005000 that
 * lives in the gap between the bootloader (0x08000000-0x08001257) and the
 * application partition (0x08005800+). The bootloader contains the
 * literal 0x08005000 and the application loads it at six sites:
 *
 *   0x08012B70 / 0x08012B78 / 0x08012B82   (early init path)
 *   0x08063A04 / 0x08063A0C / 0x08063A16   (read-modify-write path)
 *
 * Each site follows the same shape:
 *
 *   ldr r0, =0x08005000
 *   bl  <read_fn>          ; 0x08070C9C
 *   ldr r1, =0x12345678
 *   ldr r0, =0x08005000
 *   bl  <write_fn>         ; 0x08070DF0
 *   ...
 *   ldr r0, =0x08005000
 *   adds r0, r0, #4
 *   bl  <write_fn>
 *
 * so 0x12345678 is a magic/marker word stored immediately before the
 * block, and the block itself starts at 0x08005004 (the +4 offset). The
 * bootloader at 0x08000BBC also holds the literal 0x08005000, confirming
 * both sides agree on the address.
 *
 * The exact field layout is not recoverable from the image alone - the
 * bootloader half of the contract was not supplied. What is recoverable is
 * the envelope: a magic word of 0x12345678 at offset 0 and a payload
 * beginning at offset 4. The struct below models that envelope and leaves
 * the payload opaque; see docs/param-block.md for the open questions.
 *
 * Because the block lives in flash it must be rewritten through the
 * FLASH controller whenever it changes, so writes go through
 * param_block_store() which performs an erase/program cycle.
 */

#ifndef PARAM_BLOCK_H
#define PARAM_BLOCK_H

#include <stdint.h>

#define PARAM_BLOCK_ADDR      0x08005000UL
#define PARAM_BLOCK_MAGIC     0x12345678UL
#define PARAM_BLOCK_PAYLOAD   0x08005004UL
#define PARAM_BLOCK_MAX_SIZE  2048UL   /* 8 KiB region up to 0x080057FF */

/* Status of the last store attempt. */
typedef enum
{
    PARAM_OK = 0,
    PARAM_ERR_MAGIC,        /* stored magic did not match */
    PARAM_ERR_FLASH,        /* FLASH controller reported an error */
    PARAM_ERR_SIZE,         /* payload larger than the region */
} param_status_t;

/* Read the raw payload. Returns the number of bytes copied, or a negative
 * param_status_t on failure. */
int param_block_read(uint8_t *dst, uint32_t max_len);

/* Write the raw payload. The magic word is maintained automatically. */
param_status_t param_block_store(const uint8_t *src, uint32_t len);

/* Verify the magic word only. */
param_status_t param_block_check(void);

#endif /* PARAM_BLOCK_H */
