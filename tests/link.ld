/* Small address space used only for compiler/assembler/linker testing.
 * Not a radio firmware memory map or startup environment. */
MEMORY {
  text_region (rx) : ORIGIN = 0x0000, LENGTH = 0x1000
  bss_region  (rw) : ORIGIN = 0x1000, LENGTH = 0x1000
}
SECTIONS {
  text 0x0000 : { *(.text) *(.data) } >text_region
  bss  0x1000 (NOLOAD) : { *(.bss) } >bss_region
}
