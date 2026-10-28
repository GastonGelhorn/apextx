/* SPDX-License-Identifier: GPL-2.0-only */
ENTRY(Reset_Handler)
/* Preserve the retained health record at the top of system SRAM. */
MEMORY { SRAM (rwx) : ORIGIN = 0x20010000, LENGTH = 128K - 512 }
_estack = ORIGIN(SRAM) + LENGTH(SRAM);
_heap_end = _estack - 8192;
SECTIONS {
  .isr_vector : {
    _sisr_vector = .; KEEP(*(.isr_vector)) . = ALIGN(4); _eisr_vector = .;
  } > SRAM
  _isr_load = ADDR(.isr_vector);
  g_pfnVectors = _isr_load;
  .text : {
    _stext = .;
    *(.bootstrap) *(.text .text*) *(.rodata .rodata*) *(.eh_frame*)
    /* Both halves of the toolchain's startup/return sequences must survive
       --gc-sections, including the unreferenced crtn.o epilogues. */
    KEEP(*(.init)) KEEP(*(.fini))
    . = ALIGN(4);
    __init_hook_array_start = .; KEEP(*(.init_hook)) __init_hook_array_end = .;
    __preinit_array_start = .; KEEP(*(.preinit_array)) __preinit_array_end = .;
    __init_array_start = .; KEEP(*(SORT(.init_array.*))) KEEP(*(.init_array)) __init_array_end = .;
    __fini_array_start = .; KEEP(*(.fini_array)) __fini_array_end = .;
    _etext = .;
  } > SRAM
  _text_load = ADDR(.text);
  .ARM.exidx : { __exidx_start = .; *(.ARM.exidx*) __exidx_end = .; } > SRAM
  .data : { . = ALIGN(4); _sdata = .; *(.data .data*) . = ALIGN(4); _edata = .; } > SRAM
  _sidata = ADDR(.data);
  .agent_entry (NOLOAD) : { . = ALIGN(4); KEEP(*(.agent_entry)) } > SRAM
  .bss (NOLOAD) : { . = ALIGN(4); _sbss = .; *(.bss .bss*) *(COMMON) . = ALIGN(4); _ebss = .; } > SRAM
  _end = .;
  ASSERT(_end <= _heap_end, "Backup agent exceeds SRAM with its 8 KiB stack")
  /DISCARD/ : { *(.comment) }
}
