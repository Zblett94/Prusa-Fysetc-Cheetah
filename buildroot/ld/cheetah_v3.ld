/*
 * FYSETC Cheetah V3.0, STM32F446RC: 256 KiB flash, 128 KiB RAM.
 *
 * Flash sectors: 0-3 16 KiB, 4 64 KiB, 5 128 KiB. Sectors 1 and 2
 * (0x08004000..0x0800BFFF) hold the emulated EEPROM (hal_stm32/eeprom_flash.cpp),
 * so the image is split: the vector table and a few library objects fill sector 0,
 * everything else starts at sector 3.
 * Based on the linker script of the STM32duino / Marlin MARLIN_F446VE variant.
 */
ENTRY(Reset_Handler)

_estack = 0x20000000 + 128K;
_Min_Heap_Size = 0x200;
_Min_Stack_Size = 0x800;

MEMORY
{
  RAM (xrw)    : ORIGIN = 0x20000000, LENGTH = 128K
  FLASH0 (rx)  : ORIGIN = 0x08000000, LENGTH = 16K
  EEPROM (r)   : ORIGIN = 0x08004000, LENGTH = 32K
  FLASH (rx)   : ORIGIN = 0x0800C000, LENGTH = 208K
}

SECTIONS
{
  .isr_vector :
  {
    . = ALIGN(4);
    KEEP(*(.isr_vector))
    . = ALIGN(4);
  } >FLASH0

  /* code of the C/C++ runtime libraries: fills sector 0 */
  .text0 :
  {
    . = ALIGN(4);
    *libc_nano.a:*(.text .text*)
    *libgcc.a:*(.text .text*)
    *libm.a:libm_a-kf_rem_pio2.o(.text .text*)
    *libm.a:libm_a-ef_rem_pio2.o(.text .text*)
    . = ALIGN(4);
  } >FLASH0

  .text :
  {
    . = ALIGN(4);
    *(.text)
    *(.text*)
    *(.glue_7)
    *(.glue_7t)
    *(.eh_frame)
    KEEP (*(.init))
    KEEP (*(.fini))
    . = ALIGN(4);
    _etext = .;
  } >FLASH

  .rodata :
  {
    . = ALIGN(4);
    *(.rodata)
    *(.rodata*)
    . = ALIGN(4);
  } >FLASH

  .ARM.extab (READONLY) : { *(.ARM.extab* .gnu.linkonce.armextab.*) } >FLASH
  .ARM (READONLY) : {
    __exidx_start = .;
    *(.ARM.exidx*)
    __exidx_end = .;
  } >FLASH

  .preinit_array (READONLY) :
  {
    PROVIDE_HIDDEN (__preinit_array_start = .);
    KEEP (*(.preinit_array*))
    PROVIDE_HIDDEN (__preinit_array_end = .);
  } >FLASH
  .init_array (READONLY) :
  {
    PROVIDE_HIDDEN (__init_array_start = .);
    KEEP (*(SORT(.init_array.*)))
    KEEP (*(.init_array*))
    PROVIDE_HIDDEN (__init_array_end = .);
  } >FLASH
  .fini_array (READONLY) :
  {
    PROVIDE_HIDDEN (__fini_array_start = .);
    KEEP (*(SORT(.fini_array.*)))
    KEEP (*(.fini_array*))
    PROVIDE_HIDDEN (__fini_array_end = .);
  } >FLASH

  _sidata = LOADADDR(.data);

  .data :
  {
    . = ALIGN(4);
    _sdata = .;
    *(.data)
    *(.data*)
    . = ALIGN(4);
    _edata = .;
  } >RAM AT> FLASH

  . = ALIGN(4);
  .bss :
  {
    _sbss = .;
    __bss_start__ = _sbss;
    *(.bss)
    *(.bss*)
    *(COMMON)
    . = ALIGN(4);
    _ebss = .;
    __bss_end__ = _ebss;
  } >RAM

  ._user_heap_stack :
  {
    . = ALIGN(8);
    PROVIDE ( end = . );
    PROVIDE ( _end = . );
    . = . + _Min_Heap_Size;
    . = . + _Min_Stack_Size;
    . = ALIGN(8);
  } >RAM

  /DISCARD/ :
  {
    libc.a ( * )
    libm.a ( * )
    libgcc.a ( * )
  }

  .ARM.attributes 0 : { *(.ARM.attributes) }
}
