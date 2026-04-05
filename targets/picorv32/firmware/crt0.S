.section .text.start,"ax",@progbits
.globl _start
.type _start,@function
_start:
    la sp, __monitor_stack_top

    la a0, __data_start
    la a1, __data_end
    la a2, __data_load_start
1:
    bgeu a0, a1, 2f
    lw a3, 0(a2)
    sw a3, 0(a0)
    addi a0, a0, 4
    addi a2, a2, 4
    j 1b

2:
    la a0, __bss_start
    la a1, __bss_end
3:
    bgeu a0, a1, 4f
    sw zero, 0(a0)
    addi a0, a0, 4
    j 3b

4:
    call main
    call pqc_terminate
5:
    j 5b
.size _start, .-_start

.section .text.pqc_call_measured,"ax",@progbits
.globl pqc_call_measured
.type pqc_call_measured,@function
pqc_call_measured:
    addi sp, sp, -16
    sw ra, 12(sp)
    sw s0, 8(sp)
    mv s0, sp
    mv t0, a0
    mv a0, a1
    andi sp, a2, -16
    jalr t0
    mv sp, s0
    lw s0, 8(sp)
    lw ra, 12(sp)
    addi sp, sp, 16
    ret
.size pqc_call_measured, .-pqc_call_measured
