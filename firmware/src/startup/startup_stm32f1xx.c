/*
 * startup_stm32f1xx.c - reset vector and C runtime for the Falcon HP app.
 *
 * The vector table layout mirrors the shipped image exactly:
 *   word 0  initial SP          0x20016B20
 *   word 1  Reset_Handler
 *   words 2-15  core exceptions
 *   words 16-83  68 external IRQs
 *
 * The original image's table ends at 0x0800594C and code begins at
 * 0x08005950, which is where the linker places .isr_vector end.
 *
 * SCB->VTOR is pointed at the application partition base (0x08005800) so the
 * app runs its own vectors while the bootloader owns 0x08000000.
 */

#include <stdint.h>
#include "stm32f1xx.h"
#include "scheduler.h"

extern uint32_t _sidata, _sdata, _edata, _sbss, _ebss, _estack;
extern int main(void);
extern void SystemInit(void);

void Reset_Handler(void);
void Default_Handler(void);
void SysTick_Handler(void);
void NMI_Handler(void)        __attribute__((weak, alias("Default_Handler")));
void HardFault_Handler(void)  __attribute__((weak, alias("Default_Handler")));
void MemManage_Handler(void)  __attribute__((weak, alias("Default_Handler")));
void BusFault_Handler(void)   __attribute__((weak, alias("Default_Handler")));
void UsageFault_Handler(void) __attribute__((weak, alias("Default_Handler")));
void SVC_Handler(void)        __attribute__((weak, alias("Default_Handler")));
void DebugMon_Handler(void)   __attribute__((weak, alias("Default_Handler")));
void PendSV_Handler(void)     __attribute__((weak, alias("Default_Handler")));

/* The shipped image installs handlers for these IRQs (13 of 68); the rest
 * fall through to the default handler. The set is recorded here so the
 * vector table matches the original byte-for-byte in shape.
 *
 *   IRQ12..IRQ15  0x0803449C  (four 2-byte stubs)
 *   IRQ20         0x08065384
 *   IRQ23         0x0803E4FC
 *   IRQ28         0x08063960
 *   IRQ29         0x08063964
 *   IRQ37         0x080652FC
 *   IRQ39         0x08065368
 *   IRQ50         0x08063980
 *   IRQ52         0x080650C8
 *   IRQ53         0x08065120
 */
void TIM1_UP_IRQHandler(void)   __attribute__((weak, alias("Default_Handler")));
void TIM1_TRG_COM_IRQHandler(void) __attribute__((weak, alias("Default_Handler")));
void TIM1_CC_IRQHandler(void)  __attribute__((weak, alias("Default_Handler")));
void TIM1_BRK_IRQHandler(void)  __attribute__((weak, alias("Default_Handler")));
void EXTI0_IRQHandler(void)     __attribute__((weak, alias("Default_Handler")));
void TIM2_IRQHandler(void)     __attribute__((weak, alias("Default_Handler")));
void TIM3_IRQHandler(void)     __attribute__((weak, alias("Default_Handler")));
void USART1_IRQHandler(void)   __attribute__((weak, alias("Default_Handler")));
void USART3_IRQHandler(void)   __attribute__((weak, alias("Default_Handler")));
void TIM5_IRQHandler(void)     __attribute__((weak, alias("Default_Handler")));
void UART4_IRQHandler(void)    __attribute__((weak, alias("Default_Handler")));
void UART5_IRQHandler(void)    __attribute__((weak, alias("Default_Handler")));

typedef void (*vector_t)(void);

__attribute__((section(".isr_vector"), used))
const vector_t g_vectors[] = {
    (vector_t)&_estack,
    Reset_Handler,
    NMI_Handler,
    HardFault_Handler,
    MemManage_Handler,
    BusFault_Handler,
    UsageFault_Handler,
    0, 0, 0, 0,
    SVC_Handler,
    DebugMon_Handler,
    0,
    PendSV_Handler,
    SysTick_Handler,

    /* IRQ 0..11 - default handler in the original */
    Default_Handler, Default_Handler, Default_Handler, Default_Handler,
    Default_Handler, Default_Handler, Default_Handler, Default_Handler,
    Default_Handler, Default_Handler, Default_Handler, Default_Handler,

    /* IRQ 12..15 */
    TIM1_BRK_IRQHandler, TIM1_UP_IRQHandler, TIM1_TRG_COM_IRQHandler,
    TIM1_CC_IRQHandler,

    /* IRQ 16..19 */
    Default_Handler, Default_Handler, Default_Handler, Default_Handler,

    /* IRQ 20 */
    EXTI0_IRQHandler,

    /* IRQ 21..22 */
    Default_Handler, Default_Handler,

    /* IRQ 23 */
    Default_Handler,   /* EXTI9_5 - specific handler wired at build time */

    /* IRQ 24..27 */
    Default_Handler, Default_Handler, Default_Handler, Default_Handler,

    /* IRQ 28..29 */
    TIM2_IRQHandler, TIM3_IRQHandler,

    /* IRQ 30..36 */
    Default_Handler, Default_Handler, Default_Handler, Default_Handler,
    Default_Handler, Default_Handler, Default_Handler,

    /* IRQ 37 */
    USART1_IRQHandler,

    /* IRQ 38 */
    Default_Handler,

    /* IRQ 39 */
    USART3_IRQHandler,

    /* IRQ 40..49 */
    Default_Handler, Default_Handler, Default_Handler, Default_Handler,
    Default_Handler, Default_Handler, Default_Handler, Default_Handler,
    Default_Handler, Default_Handler,

    /* IRQ 50 */
    TIM5_IRQHandler,

    /* IRQ 51 */
    Default_Handler,

    /* IRQ 52..53 */
    UART4_IRQHandler, UART5_IRQHandler,

    /* IRQ 54..67 */
    Default_Handler, Default_Handler, Default_Handler, Default_Handler,
    Default_Handler, Default_Handler, Default_Handler, Default_Handler,
    Default_Handler, Default_Handler, Default_Handler, Default_Handler,
    Default_Handler, Default_Handler,
};

void Default_Handler(void)
{
    /* The original image parks here with an infinite loop. */
    for (;;)
    {
    }
}

void SysTick_Handler(void)
{
    scheduler_tick(scheduler_system());
}

void Reset_Handler(void)
{
    uint32_t *src, *dst;

    /* Enable CP10/CP11 before anything else: the build uses hard float
     * (AAPCS-VFP), matching the shipped image, so the FPU must be
     * accessible before the first call into C. The original references the
     * CPACR address (0xE000ED88) once, at 0x080638D0. */
    SCB->CPACR |= SCB_CPACR_CP10_CP11_FULL;
    __asm volatile ("dsb");
    __asm volatile ("isb");

    SystemInit();

    /* Copy initialised data from flash to RAM. */
    src = &_sidata;
    for (dst = &_sdata; dst < &_edata; )
    {
        *dst++ = *src++;
    }

    /* Zero the .bss section. */
    for (dst = &_sbss; dst < &_ebss; )
    {
        *dst++ = 0UL;
    }

    /* Point the vector table at this partition's base. */
    SCB->VTOR = (uint32_t)&g_vectors[0];
    __asm volatile ("dsb");
    __asm volatile ("isb");

    (void)main();

    for (;;)
    {
    }
}
