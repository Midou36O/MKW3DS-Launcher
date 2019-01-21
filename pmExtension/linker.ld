OUTPUT_FORMAT("elf32-littlearm", "elf32-littlearm", "elf32-littlearm")
OUTPUT_ARCH(arm)

ENTRY(_start)

SECTIONS
{
  . = 0x0010BA00;

  __start__ = ABSOLUTE(.);

  .text       : { build/start.o (.text*) *(.text) }
  .bss        : { *(.bss) }
  .rodata     : { *(.rodata) }
  .data       : { *(.data) }
  __end__ = .;
}
