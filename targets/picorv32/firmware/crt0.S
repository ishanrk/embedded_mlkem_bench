.section .text.start,"ax",@progbits
.globl _start
.type _start,@function
_start:
    # entry starts before any operating system or C runtime setup
    la sp, __stack_top

    # copies initialized global values into writable memory
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
    # clears uninitialized global storage before main
    la a0, __bss_start
    la a1, __bss_end
3:
    bgeu a0, a1, 4f
    sw zero, 0(a0)
    addi a0, a0, 4
    j 3b

4:
    # passes the main return value to the termination function in register a0
    call main
    call pqc_terminate
5:
    j 5b
.size _start, .-_start
