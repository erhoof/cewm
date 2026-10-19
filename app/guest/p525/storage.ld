/* Static P525 OAT window, outside the ROM and DRAM used by Windows Mobile. */
ENTRY(DSK_Init)
SECTIONS {
    . = 0x80010000;
    .text : { *(.text*) *(.rodata*) }
    /DISCARD/ : { *(.ARM.exidx*) *(.ARM.extab*) *(.comment*) *(.note*) }
}
