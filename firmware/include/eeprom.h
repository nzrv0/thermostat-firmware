/*
 * eeprom.h - AT24C256 EEPROM on the bit-banged I2C bus.
 *
 * The device is an AT24C256: 32 KiB, I2C address 0xA0 (write) / 0xA1
 * (read). The shipped firmware reads it through the API at 0x08042938,
 * which refuses addresses at or beyond 0x8000 and answers with the two
 * bytes it read. The boot-time self-test reads two bytes at address 1
 * and expects the signature "#E" (0x23 0x45).
 */

#ifndef EEPROM_H
#define EEPROM_H

#include <stdint.h>
#include "i2c.h"

#define EEPROM_SIZE_BYTES        32768u
#define EEPROM_I2C_ADDR_WRITE    0xA0u
#define EEPROM_I2C_ADDR_READ     0xA1u

/* Reads len bytes starting at addr. Returns the number of bytes read,
 * or 0 when the request is out of range or the device does not
 * acknowledge. */
uint16_t eeprom_read(i2c_bus_t *bus, uint16_t addr, uint8_t *buf, uint16_t len);

/* True when the device answers with the "#E" signature at address 1,
 * which is what the shipped self-test checks. */
int eeprom_check(i2c_bus_t *bus);

#endif /* EEPROM_H */
