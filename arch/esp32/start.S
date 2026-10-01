.section .iram0.text,"ax"
.align 4

.global blockos_arch_entry
.type blockos_arch_entry,@function

/*
 * Entry point for the ESP32 application image.
 *
 * The immutable ROM bootloader loads the second-stage bootloader. The
 * second-stage bootloader loads the application segments and jumps here.
 *
 * We deliberately do not attempt to replace the ROM reset vector.
 */
blockos_arch_entry:
    mov     a2, a1

    call0   blockos_arch_start

1:
    waiti   0
    j       1b

.size blockos_arch_entry, .-blockos_arch_entry

.global blockos_arch_start
.type blockos_arch_start,@function

blockos_arch_start:
    call0   blockos_arch_start_c

2:
    waiti   0
    j       2b

.size blockos_arch_start, .-blockos_arch_start
