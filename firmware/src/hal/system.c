/*
 * system.c - clock bring-up and system helpers.
 *
 * The shipped image runs at 72 MHz from an 8 MHz HSE with the PLL x9, which
 * is the standard F103 configuration: HSE -> PLL (x9) -> SYSCLK 72 MHz,
 * AHB /1, APB1 /2 (36 MHz), APB2 /1 (72 MHz).
 *
 * The image references RCC_CR.HSEON/HSERDY and RCC_CFGR.SWS repeatedly
 * (RCC base 0x40021000 appears 99 times in literal pools), and the
 * bootloader's flash-write path at 0x0800071C computes flash page addresses
 * from the same RCC/FLASH block, confirming a standard F1 clock tree.
 */

#include "stm32f1xx.h"
#include "system.h"

void SystemInit(void)
{
    uint32_t timeout;

    /* Enable the high-speed external oscillator. */
    RCC->CR |= RCC_CR_HSEON;
    for (timeout = 0; timeout < HSE_STARTUP_TIMEOUT; timeout++)
    {
        if (RCC->CR & RCC_CR_HSERDY)
        {
            break;
        }
    }

    /* Pre-scale the buses before switching the core clock, so the bus
     * dividers are already in place when the PLL takes over. */
    RCC->CFGR = RCC_CFGR_HPRE_DIV1 | RCC_CFGR_PPRE1_DIV2;

    /* PLL: HSE as source, x9 multiplier -> 72 MHz. */
    RCC->CFGR &= ~(RCC_CFGR_PLLSRC_HSE | RCC_CFGR_PLLXTPRE_DIV1 |
                   RCC_CFGR_PLLMULL_Msk);
    RCC->CFGR |= RCC_CFGR_PLLSRC_HSE | RCC_CFGR_PLLXTPRE_DIV1 |
                 RCC_CFGR_PLLMULL_MUL9;
    RCC->CR |= RCC_CR_PLLON;

    for (timeout = 0; timeout < PLL_STARTUP_TIMEOUT; timeout++)
    {
        if (RCC->CR & RCC_CR_PLLRDY)
        {
            break;
        }
    }

    /* Switch SYSCLK to the PLL and wait for the switch to complete. */
    RCC->CFGR = (RCC->CFGR & ~RCC_CFGR_SW_Msk) | RCC_CFGR_SW_PLL;
    while ((RCC->CFGR & RCC_CFGR_SWS_Msk) != RCC_CFGR_SWS_PLL)
    {
    }

    /* The shipped image's reset handler jumps through a vector at
     * 0x08005B44 = 0x08063861 before entering main; that routine enables
     * the AFIO/GPIO clocks. Do the same here so the GPIO pins are usable
     * by the drivers. */
    RCC->APB2ENR |= RCC_APB2ENR_AFIOEN | RCC_APB2ENR_IOPAEN |
                    RCC_APB2ENR_IOPBEN | RCC_APB2ENR_IOPCEN |
                    RCC_APB2ENR_IOPDEN | RCC_APB2ENR_IOPEEN |
                    RCC_APB2ENR_IOPFEN | RCC_APB2ENR_IOPGEN;
    RCC->APB1ENR |= RCC_APB1ENR_UART4EN | RCC_APB1ENR_UART5EN;
    RCC->APB2ENR |= RCC_APB2ENR_USART1EN | RCC_APB2ENR_USART3EN;
}

void system_reset(void)
{
    __disable_irq();
    SCB->AIRCR = (0x5FAUL << SCB_AIRCR_VECTKEY_Pos) | SCB_AIRCR_SYSRESETREQ;
    __asm volatile ("dsb");
    for (;;)
    {
    }
}

void delay_cycles(uint32_t cycles)
{
    while (cycles--)
    {
        __asm volatile ("nop");
    }
}

void delay_ms(uint32_t ms)
{
    /* Free-running approximation on the 72 MHz core. Accurate enough for
     * the reset/settle delays the drivers need; the scheduler uses the
     * SysTick for anything that must be precise. */
    while (ms--)
    {
        delay_cycles(CORE_CYCLES_PER_MS / 10);
    }
}
