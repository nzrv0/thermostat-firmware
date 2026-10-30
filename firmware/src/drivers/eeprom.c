/*
 * eeprom.c - AT24C256 driver on the bit-banged I2C bus.
 *
 * The transaction sequence mirrors the shipped implementation at
 * 0x08042938:
 *
 *   START, 0xA0, addr_hi, addr_lo, repeated START, 0xA1, data..., STOP
 *
 * The original does not look at the acknowledge bits; this driver does,
 * and reports a failure instead of returning garbage.
 */

#include "eeprom.h"

uint16_t eeprom_read(i2c_bus_t *bus, uint16_t addr, uint8_t *buf, uint16_t len)
{
    uint16_t i;

    if (bus == 0 || buf == 0 || len == 0)
    {
        return 0;
    }
    if ((uint32_t)addr + len > EEPROM_SIZE_BYTES)
    {
        return 0;
    }

    i2c_start(bus);
    if (!i2c_write_byte(bus, EEPROM_I2C_ADDR_WRITE))
    {
        i2c_stop(bus);
        return 0;
    }
    if (!i2c_write_byte(bus, (uint8_t)(addr >> 8)))
    {
        i2c_stop(bus);
        return 0;
    }
    if (!i2c_write_byte(bus, (uint8_t)(addr & 0xFF)))
    {
        i2c_stop(bus);
        return 0;
    }

    i2c_start(bus);
    if (!i2c_write_byte(bus, EEPROM_I2C_ADDR_READ))
    {
        i2c_stop(bus);
        return 0;
    }

    for (i = 0; i < len; i++)
    {
        buf[i] = i2c_read_byte(bus, (i + 1 < len) ? 1 : 0);
    }

    i2c_stop(bus);
    return len;
}

int eeprom_check(i2c_bus_t *bus)
{
    uint8_t sig[2];

    if (eeprom_read(bus, 1, sig, 2) != 2)
    {
        return 0;
    }
    return (sig[0] == 0x23 && sig[1] == 0x45) ? 1 : 0;
}
