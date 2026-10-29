/* Linker script for the Falcon HP thermostat application.
 *
 * Memory map reconstructed from the shipped firmware:
 *
 *   0x08000000 - 0x08001257  bootloader code      (at32_bootloader_6M.hex)
 *   0x08001258 - 0x08004FFF  reserved / erased
 *   0x08005000 - 0x080057FF  shared parameter block (bootloader <-> app contract)
 *   0x08005800 - 0x08094237  application          (Falcon-HP-144pin-8M-Mark-261001.hex)
 *
 * The application is linked to start exactly at 0x08005800 because the
 * bootloader jumps to that address; the vector table lives at the very
 * start of the application partition and SCB->VTOR is pointed at it.
 *
 * SRAM: 0x20000000..0x20018000 (96 KiB). The original image's initial
 * stack pointer is 0x20016B20, which is reproduced verbatim below.
 */

ENTRY(Reset_Handler)

MEMORY
{
    /* Application partition of the internal flash. */
    FLASH (rx)  : ORIGIN = 0x08005800, LENGTH = 1024K - 0x5800
    /* 96 KiB SRAM on the high-density F103 / AT32F4xx-class parts. */
    RAM   (rwx) : ORIGIN = 0x20000000, LENGTH = 96K
}

/* Stack top: identical to the shipped image (vector table word 0). */
_estack = ORIGIN(RAM) + 0x16B20;
_min_stack_size = 0x1000;

/* The original image places its vector table at the partition base and
 * relocates SCB->VTOR to it at startup. */
_vector_table_base = ORIGIN(FLASH);

_Min_Heap_Size  = 0x200;
_Min_Stack_Size = 0x800;

SECTIONS
{
    .isr_vector :
    {
        . = ALIGN(4);
        KEEP(*(.isr_vector))
        . = ALIGN(4);
    } >FLASH

    .text :
    {
        . = ALIGN(4);
        *(.text)
        *(.text*)
        *(.glue_7)
        *(.glue_7t)
        *(.eh_frame)
        KEEP(*(.init))
        KEEP(*(.fini))
        . = ALIGN(4);
        _etext = .;
    } >FLASH

    .rodata :
    {
        . = ALIGN(4);
        *(.rodata)
        *(.rodata*)
        . = ALIGN(4);
    } >FLASH

    /* ARM-specific unwind tables; discarded to save flash. */
    .ARM.extab : { *(.ARM.extab* .gnu.linkonce.armextab.*) } >FLASH
    .ARM :
    {
        __exidx_start = .;
        *(.ARM.exidx*)
        __exidx_end = .;
    } >FLASH

    .preinit_array :
    {
        PROVIDE_HIDDEN(__preinit_array_start = .);
        KEEP(*(.preinit_array*))
        PROVIDE_HIDDEN(__preinit_array_end = .);
    } >FLASH
    .init_array :
    {
        PROVIDE_HIDDEN(__init_array_start = .);
        KEEP(*(SORT(.init_array.*)))
        KEEP(*(.init_array*))
        PROVIDE_HIDDEN(__init_array_end = .);
    } >FLASH
    .fini_array :
    {
        PROVIDE_HIDDEN(__fini_array_start = .);
        KEEP(*(SORT(.fini_array.*)))
        KEEP(*(.fini_array*))
        PROVIDE_HIDDEN(__fini_array_end = .);
    } >FLASH

    _sidata = LOADADDR(.data);

    /* Initialised data: stored in flash, copied to RAM by startup code. */
    .data :
    {
        . = ALIGN(4);
        _sdata = .;
        *(.data)
        *(.data*)
        . = ALIGN(4);
        _edata = .;
    } >RAM AT> FLASH

    .bss :
    {
        . = ALIGN(4);
        _sbss = .;
        __bss_start__ = _sbss;
        *(.bss)
        *(.bss*)
        *(COMMON)
        . = ALIGN(4);
        _ebss = .;
        __bss_end__ = _ebss;
    } >RAM

    ._user_heap_stack :
    {
        . = ALIGN(8);
        PROVIDE(end = .);
        PROVIDE(_end = .);
        . = . + _Min_Heap_Size;
        . = . + _Min_Stack_Size;
        . = ALIGN(8);
    } >RAM

    /DISCARD/ :
    {
        libc.a(*)
        libm.a(*)
        libgcc.a(*)
    }

    .ARM.attributes 0 : { *(.ARM.attributes) }
}
