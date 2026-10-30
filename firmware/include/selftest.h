/*
 * selftest.h - boot-time hardware check.
 *
 * The shipped image runs an ordered self-test at boot (the strings live
 * at 0x08016013-0x080161A0):
 *
 *   "Hardware Checking..."
 *   "1. Relay: T1_On"
 *   "2. EEPROM AT24C256 ok." / "2. EEPROM AT24C256 error."
 *   "3. Norflash SST39VF1601 ok." / "... error."
 *   "4. RTC S35390 ok." / " 4. RTC S35390 error."
 *   " 5. Humidity SI7021 fail"
 *   " 5. WIFI ESP-07S ok, v%s"
 *   " 7. On Board NTC: %d.%dc" ... "10. Light Sensor: %d"
 *
 * The rebuild covers the relay and EEPROM steps, which are the two whose
 * paths are recovered (the relay pins from the point registry, the EEPROM
 * bus from the bit-banged I2C driver).
 */

#ifndef SELFTEST_H
#define SELFTEST_H

#include <stdint.h>
#include "i2c.h"

#define SELFTEST_RELAY_OK    (1u << 0)
#define SELFTEST_EEPROM_OK   (1u << 1)

/* Drives the W1 output and reads it back, the way the original's
 * "1. Relay: T1_On" step does. Hardware only. */
uint32_t selftest_relay(void);

/* Reads the EEPROM signature the original checks ("#E" at address 1). */
uint32_t selftest_eeprom(i2c_bus_t *bus);

/* The full boot check. */
uint32_t selftest_run(i2c_bus_t *eeprom_bus);

#endif /* SELFTEST_H */
