/*
 * Included by the x86-64 interrupt and syscall entry stubs.
 * At invocation the stack points to the saved CPU IRET frame:
 *   (%rsp) = RIP, 8(%rsp) = CS.
 */

.macro BLOCKOS_ENTER_KERNEL_SPEC_CTRL cs_offset
    testb $3, \cs_offset(%rsp)
    jz 9f
    call blockos_cpu_security_enter_kernel
9:
.endm

.macro BLOCKOS_LEAVE_KERNEL_SPEC_CTRL cs_offset
    testb $3, \cs_offset(%rsp)
    jz 9f
    call blockos_cpu_security_leave_kernel
9:
.endm

.macro BLOCKOS_CLEAR_CPU_BUFFERS
    /* VERW has the MD_CLEAR buffer-clearing effect only when CPUID advertises it. */
    testb $3, 8(%rsp)
    jz 2f
    push %rax
    push %rbx
    push %rcx
    push %rdx
    xor %eax, %eax
    cpuid
    cmp $7, %eax
    jb 1f
    mov $7, %eax
    xor %ecx, %ecx
    cpuid
    testl $0x400, %edx
    jz 1f
    mov $0x10, %ax             /* valid BlockOS kernel data selector */
    verw %ax
1:
    pop %rdx
    pop %rcx
    pop %rbx
    pop %rax
2:
.endm
