/*
 * main.c - application entry point.
 *
 * The shipped image's reset handler at 0x08005B1C does:
 *
 *   ldr r0, [pc, #0x24]   ; = 0x08063861
 *   blx r0                ; SystemInit
 *   ldr r0, [pc, #0x24]   ; = 0x08005951  (the __main veneer)
 *   bx  r0
 *
 * and the veneer at 0x08005950 walks the scatter table, calls
 * __scatterload (0x08005AC8) and branches to main. This build reproduces
 * that shape: SystemInit() runs from the reset handler, then main() is
 * entered with the stack pointer and vector table already set up.
 *
 * The self-test strings in the image ("Hardware Checking...") show the
 * original runs an ordered hardware check at boot. That behaviour is
 * reproduced by board_init() and the driver probes that follow it.
 */

#include "stm32f1xx.h"
#include "system.h"
#include "board.h"
#include "hvac.h"
#include "param_block.h"
#include "i2c.h"
#include "eeprom.h"
#include "selftest.h"
#include "scheduler.h"

int main(void)
{
    i2c_bus_t eeprom_bus;

    board_init();
    hvac_init();

    /* The boot check the original runs before anything else: relay
     * outputs first, then the EEPROM signature over the bit-banged I2C
     * bus on GPIOG. */
    i2c_bus_init(&eeprom_bus, board_i2c_sda, board_i2c_scl);
    (void)selftest_run(&eeprom_bus);

    /* The original runs a bare-metal super loop fed by a tick, with a
     * window message layer on top; this rebuild uses the same shape. */
    scheduler_init(scheduler_system());
    scheduler_start(72000000UL);

    /* The shared parameter block is written by the bootloader. If it is
     * absent or corrupt the firmware must fall back to defaults rather
     * than read garbage; see docs/param-block.md. */
    if (param_block_check() != PARAM_OK)
    {
        /* Defaults are already in place from hvac_init(). */
    }

    for (;;)
    {
        scheduler_post(scheduler_system(), WM_TICK, 0, 0);
        scheduler_post(scheduler_system(), WM_HVAC_EVALUATE, 0, 0);
        scheduler_run(scheduler_system());
        hvac_tick(1);
        __asm volatile ("wfi");
    }
}
