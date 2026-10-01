/* BlockOS classic ESP32 Xtensa linker script.
 *
 * This targets the classic ESP32 memory layout used by the Xtensa LX6
 * application image. The immutable ROM bootloader + second-stage
 * bootloader remain responsible for image loading and flash-MMU setup.
 */

ENTRY(blockos_arch_entry)

MEMORY
{
    iram (rx)  : ORIGIN = 0x40080000, LENGTH = 0x20000
    dram (rw)  : ORIGIN = 0x3FFB0000, LENGTH = 0x40000
}

SECTIONS
{
    .iram0.text :
    {
        . = ALIGN(4);
        __text_start = .;
        KEEP(*(.iram0.text))
        *(.text .text.*)
        *(.literal .literal.*)
        . = ALIGN(4);
        __text_end = .;
    } > iram

    .rodata :
    {
        . = ALIGN(4);
        __rodata_start = .;
        *(.rodata .rodata.*)
        . = ALIGN(4);
        __rodata_end = .;
    } > iram

    .data :
    {
        . = ALIGN(4);
        __data_start = .;
        *(.data .data.*)
        . = ALIGN(4);
        __data_end = .;
    } > dram AT > iram

    .bss (NOLOAD) :
    {
        . = ALIGN(16);
        _bss_start = .;
        *(.bss .bss.*)
        *(COMMON)
        . = ALIGN(16);
        _bss_end = .;
    } > dram

    .noinit (NOLOAD) :
    {
        . = ALIGN(16);
        *(.noinit .noinit.*)
        . = ALIGN(16);
    } > dram

    .heap :
    {
        . = ALIGN(16);
        _heap_start = .;
        . += 0x10000;
        . = ALIGN(16);
        _heap_end = .;
    } > dram

    .stack (NOLOAD) :
    {
        . = ALIGN(16);
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
    }

    ASSERT(
        _stack_top <= ORIGIN(dram) + LENGTH(dram),
        "BlockOS ESP32 stack exceeds DRAM"
    )

    ASSERT(
        __text_end <= ORIGIN(iram) + LENGTH(iram),
        "BlockOS ESP32 IRAM overflow"
    )
}
