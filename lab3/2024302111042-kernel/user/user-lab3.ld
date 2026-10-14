/* Derived lab3 linker: preserve immutable user.ld, but page-align writable
 * data so SV39 can enforce RX code and RW data permissions. */
OUTPUT_ARCH(riscv)
ENTRY(main)

SECTIONS
{
  . = 0x0;
  .text : { *(.text .text.*) }
  .rodata : { *(.rodata .rodata.*) }
  . = ALIGN(0x1000);
  data_start = .;
  .data : { *(.data .data.*) }
  .bss : {
    *(.bss .bss.*)
    *(.sbss*)
  }
  end = .;
}
