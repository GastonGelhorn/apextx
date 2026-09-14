
/* Highest address of the user mode stack.
 *
 * Kept 16 bytes below the end of CCM so it cannot reach .reboot_buffer, which
 * common_sections.ld places in the last word of REGION_DATA. With the stack
 * starting at the very end of the region, the first push after a reset landed
 * on that word and destroyed any reboot command before the bootloader could
 * read it. */
STACK_ADDRESS = 0x1000FFF0; /* end of 64K CCM, less the reboot buffer */

/* Required amount of stack for interrupt stack (Main stack) */
MAIN_STACK_SIZE = 8192;

/* Maximum bootloader code size */
BOOTLOADER_SIZE = 0x20000;

/* Minimum Heap Size (indicative) */
MIN_HEAP_SIZE = 0;

/* SDRAM definitions */
EXTRAM_START = DEFINED(__EXTRAM_START__) ? __EXTRAM_START__ : 0xD0000000;
EXTRAM_SIZE = DEFINED(__EXTRAM_SIZE__) ? __EXTRAM_SIZE__ : 8192K;

/* Highest heap address */
HEAP_ADDRESS = EXTRAM_START + EXTRAM_SIZE;

/* Specify the memory areas */
MEMORY
{
  FLASH (rx)      : ORIGIN = 0x08000000, LENGTH = 2048K
  RAM (xrw)       : ORIGIN = 0x20000000, LENGTH = 192K - (DEFINED(__NB4_HEALTH_SIZE__) ? __NB4_HEALTH_SIZE__ : 0)
  HEALTH (rw)     : ORIGIN = 0x20000000 + 192K - (DEFINED(__NB4_HEALTH_SIZE__) ? __NB4_HEALTH_SIZE__ : 0), LENGTH = DEFINED(__NB4_HEALTH_SIZE__) ? __NB4_HEALTH_SIZE__ : 0
  CCM (xrw)       : ORIGIN = 0x10000000, LENGTH = 64K
  SDRAM(xrw)      : ORIGIN = EXTRAM_START, LENGTH = EXTRAM_SIZE
}

REGION_ALIAS("REGION_BOOTLOADER",   FLASH);
REGION_ALIAS("REGION_ISR_VECT",     FLASH);
REGION_ALIAS("REGION_TEXT",         FLASH);
REGION_ALIAS("REGION_TEXT_STORAGE", FLASH);

REGION_ALIAS("REGION_DATA", CCM);
REGION_ALIAS("REGION_BSS", RAM);
REGION_ALIAS("REGION_RAM", RAM);
