OUTPUT_ARCH(riscv)
ENTRY(_start)

/* one flat 512 KiB memory: code and data share the simulator's RAM */
MEMORY
{
    RAM (rwx) : ORIGIN = 0x00000000, LENGTH = 512K
}

SECTIONS
{
    /* `> RAM` assigns each output section to that memory region */
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

    /* NOLOAD occupies RAM but adds no zero bytes to the firmware image */
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

    /* metadata the bare-metal image cannot use */
    /DISCARD/ :
    {
        *(.comment .note .note.* .eh_frame .eh_frame.*)
    }

    __image_end = ALIGN(16);
    /* keep benchmark stack writes away from the monitor stack reading the watermark */
    __measured_stack_bottom = 0x00060000;
    __measured_stack_top = 0x00070000;
    __monitor_stack_bottom = 0x00070000;
    __monitor_stack_top = 0x00080000;

    /* fail at link time rather than letting a larger image corrupt the stack experiment */
    ASSERT(__image_end <= __measured_stack_bottom, "firmware overlaps measured stack")
}
