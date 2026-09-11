OUTPUT_ARCH(riscv)
ENTRY(_start)

/* code and data share one 512 KiB simulator memory */
MEMORY
{
    RAM (rwx) : ORIGIN = 0x00000000, LENGTH = 512K
}

SECTIONS
{
    /* places each output section in the RAM region */
    .text :
    {
        KEEP(*(.text.start))
        *(.text .text.*)
    } > RAM

    .rodata :
    {
        *(.rodata .rodata.* .srodata .srodata.*)
    } > RAM

    .data : ALIGN(4)
    {
        __data_start = .;
        *(.data .data.* .sdata .sdata.*)
        . = ALIGN(4);
        __data_end = .;
    } > RAM
    __data_load_start = LOADADDR(.data);

    /* NOLOAD reserves RAM without adding bytes to the firmware image */
    .bss (NOLOAD) : ALIGN(4)
    {
        __bss_start = .;
        *(.bss .bss.* .sbss .sbss.* COMMON)
        . = ALIGN(4);
        __bss_end = .;
    } > RAM

    .riscv.attributes 0 :
    {
        *(.riscv.attributes)
    }

    .rela 0 :
    {
        *(.rela .rela.*)
    }

    /* removes metadata unused by the firmware */
    /DISCARD/ :
    {
        *(.comment .note .note.* .eh_frame .eh_frame.*)
    }

    __image_end = ALIGN(16);
    /* separates the measured stack from the monitor stack */
    __measured_stack_bottom = 0x00060000;
    __measured_stack_top = 0x00070000;
    __monitor_stack_bottom = 0x00070000;
    __monitor_stack_top = 0x00080000;

    /* rejects firmware that overlaps the measured stack */
    ASSERT(__image_end <= __measured_stack_bottom, "firmware overlaps measured stack")
}
