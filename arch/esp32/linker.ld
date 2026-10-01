/*
 * BlockOS classic ESP32 application-image linker script.
 *
 * The ESP32 ROM bootloader loads a software bootloader, which loads the
 * application image and jumps to its entry point. This target keeps the
 * BlockOS image entirely in internal IRAM/DRAM so no ESP-IDF runtime is
 * required for the initial platform bring-up.
 */

ENTRY(blockos_arch_entry)

MEMORY
{
    iram (rx)  : ORIGIN = 0x40080000, LENGTH = 0x20000
    dram (rw)  : ORIGIN = 0x3FFB0000, LENGTH = 0x40000
}

SECTIONS
{
    .text : ALIGN(4)
    {
        __text_start = .;
        KEEP(*(.text.start))
        KEEP(*(.iram0.text))
        *(.literal .literal.*)
        *(.text .text.*)
        . = ALIGN(4);
        __text_end = .;
    } > iram

    .rodata : ALIGN(4)
    {
        __rodata_start = .;
        *(.rodata .rodata.*)
        . = ALIGN(4);
        __rodata_end = .;
    } > iram

    .data : ALIGN(4)
    {
        __data_start = .;
        *(.data .data.*)
        *(.sdata .sdata.*)
        . = ALIGN(4);
        __data_end = .;
    } > dram

    .bss (NOLOAD) : ALIGN(16)
    {
        _bss_start = .;
        *(.bss .bss.*)
        *(COMMON)
        . = ALIGN(16);
        _bss_end = .;
    } > dram

    .noinit (NOLOAD) : ALIGN(16)
    {
        *(.noinit .noinit.*)
        . = ALIGN(16);
    } > dram

    .heap (NOLOAD) : ALIGN(16)
    {
        _heap_start = .;
        . += 0x10000;
        . = ALIGN(16);
        _heap_end = .;
    } > dram

    .stack (NOLOAD) : ALIGN(16)
    {
        _stack_bottom = .;
        . += 0x8000;
        . = ALIGN(16);
        _stack_top = .;
    } > dram

    /DISCARD/ :
    {
        *(.eh_frame)
        *(.gcc_except_table)
        *(.comment)
        *(.xtensa.info)
        *(.xt.lit)
    }

    ASSERT(__text_end <= ORIGIN(iram) + LENGTH(iram),
           "BlockOS ESP32 IRAM overflow")
    ASSERT(_stack_top <= ORIGIN(dram) + LENGTH(dram),
           "BlockOS ESP32 DRAM overflow")
}
