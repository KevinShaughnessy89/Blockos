.section .text.start,"ax"
.align 4

.global blockos_arch_entry
.type blockos_arch_entry,@function

blockos_arch_entry:
    /* Use the statically reserved BlockOS stack. */
    movi    a1, _stack_top
    call8   blockos_arch_start_c

1:
    waiti   0
    j       1b

.size blockos_arch_entry, .-blockos_arch_entry
