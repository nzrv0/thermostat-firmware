/*
 * adc.h - the MCU's ADC.
 *
 * The ADC is not at the STM32F1 address: the shipped firmware uses the
 * AT32 address 0x40013C00 (the STM32F1 ADC1 at 0x40012400 is referenced
 * once, at 0x08066B18, for a different peripheral). The register
 * accesses recovered from the original are:
 *
 *   +0x00  control 1        (bit 0 and bit 1 are cleared before a read)
 *   +0x04  control 2        (bit 8 = DMA request enable)
 *   +0x08  control 3        (bit 2 = end of conversion, bit 3 = start,
 *                            bit 22 = enable, bits 17:19 = trigger
 *                            source, bits 24:28 = trigger edge)
 *   +0x0C  channel select   (eight three-bit fields)
 *   +0x10  channel select   (ten three-bit fields)
 *   +0x2C  sample time      (four-bit field at bits 20:23)
 *   +0x34  sequence         (five-bit channel fields)
 *   +0x4C  data             (16-bit result)
 *
 * The original's read path (0x08071178) selects a channel, enables the
 * converter and returns 0xFFF minus the data register, so the counts are
 * inverted. The driver below reproduces that.
 */

#ifndef ADC_H
#define ADC_H

#include <stdint.h>

#define ADC_BASE_ADDR     0x40013C00UL
#define ADC_FULL_SCALE    0x0FFFUL

/* Brings the ADC up: peripheral clock, reset, analog pins. */
void adc_init(void);

/* Selects a logical channel and returns its raw count (0..4095).
 *
 * logical 0..9 map to the three-bit fields of the select register at
 * +0x10, logical 10..17 to those at +0x0C, which is how the original's
 * dispatch table (0x08066BFC) spreads its eighteen channels. */
uint16_t adc_read(uint8_t logical, uint8_t channel);

#endif /* ADC_H */
