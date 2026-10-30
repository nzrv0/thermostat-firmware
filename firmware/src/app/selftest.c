/*
 * selftest.c - boot-time hardware check.
 */

#include "selftest.h"
#include "board.h"
#include "eeprom.h"
#include "gpio.h"

uint32_t selftest_relay(void)
{
    uint32_t result = 0;

    /* The original publishes the relay state by reading the same pin it
     * drives (W1 is PA3), so the check is: drive, read back, release. */
    gpio_write(board_relay_w1, 1);
    if (gpio_read(board_relay_w1))
    {
        result |= SELFTEST_RELAY_OK;
    }
    gpio_write(board_relay_w1, 0);

    return result;
}

uint32_t selftest_eeprom(i2c_bus_t *bus)
{
    if (eeprom_check(bus))
    {
        return SELFTEST_EEPROM_OK;
    }
    return 0;
}

uint32_t selftest_run(i2c_bus_t *eeprom_bus)
{
    uint32_t result = selftest_relay();

    result |= selftest_eeprom(eeprom_bus);
    return result;
}
