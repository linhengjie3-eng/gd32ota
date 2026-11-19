ENTRY(Reset_Handler)

MEMORY
{
    FLASH (rx)  : ORIGIN = 0x08000000, LENGTH = 16K
    RAM (xrw)   : ORIGIN = 0x20000000, LENGTH = 96K
}

_estack = ORIGIN(RAM) + LENGTH(RAM);
_Min_Heap_Size = 0x200;
_Min_Stack_Size = 0x800;

SECTIONS
{
    .isr_vector : ALIGN(4)
    {
        KEEP(*(.isr_vector))
    } >FLASH
    .text : ALIGN(4)
    {
        *(.text*) *(.rodata*) *(.glue_7) *(.glue_7t)
        KEEP(*(.init)) KEEP(*(.fini))
        _etext = .;
    } >FLASH
    .ARM.extab : { *(.ARM.extab* .gnu.linkonce.armextab.*) } >FLASH
    .ARM : { __exidx_start = .; *(.ARM.exidx*) __exidx_end = .; } >FLASH
    .preinit_array : { PROVIDE_HIDDEN(__preinit_array_start = .); KEEP(*(.preinit_array*)) PROVIDE_HIDDEN(__preinit_array_end = .); } >FLASH
    .init_array : { PROVIDE_HIDDEN(__init_array_start = .); KEEP(*(SORT(.init_array.*))) KEEP(*(.init_array*)) PROVIDE_HIDDEN(__init_array_end = .); } >FLASH
    .fini_array : { PROVIDE_HIDDEN(__fini_array_start = .); KEEP(*(SORT(.fini_array.*))) KEEP(*(.fini_array*)) PROVIDE_HIDDEN(__fini_array_end = .); } >FLASH
    _sidata = LOADADDR(.data);
    .data : ALIGN(4)
    {
        _sdata = .; *(.data*) _edata = .;
    } >RAM AT>FLASH
    . = ALIGN(4);
    .bss : { _sbss = .; __bss_start__ = .; *(.bss*) *(COMMON) _ebss = .; __bss_end__ = .; _end = .; end = .; } >RAM
    ._user_heap_stack : ALIGN(8)
    {
        . = . + _Min_Heap_Size;
        . = . + _Min_Stack_Size;
    } >RAM
    /DISCARD/ : { libc.a(lib_a-exit.o) libnosys.a(*) }
    .ARM.attributes 0 : { *(.ARM.attributes) }
    __boot_image_end = LOADADDR(.data) + SIZEOF(.data);
}
ASSERT(__boot_image_end <= ORIGIN(FLASH) + LENGTH(FLASH), "Bootloader exceeds 16 KiB")
