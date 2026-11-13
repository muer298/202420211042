/* Derived lab3 linker: preserve immutable user.ld, but page-align writable
 * data so SV39 can enforce RX code and RW data permissions. 
 链接脚本把用户程序链接到虚拟地址 0，并在 .text、.rodata 后将地址对齐到 4KB，
 再定义 data_start；.data、.bss 之后定义 end*/
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
