/* SPDX-License-Identifier: GPL-2.0-or-later */
ENTRY(__efi_entry_descriptor)

SECTIONS
{
    . = 0x04000000;
    __image_base = .;

    .text : { *(.text*) }
    . = ALIGN(16);
    .rodata : { *(.rodata*) }
    . = ALIGN(16);
    .opd : {
        __efi_entry_descriptor = .;
        *(.opd)
    }
    . = ALIGN(16);
    .data : { *(.data*) }
    . = ALIGN(16);
    .got : { *(.got*) }
    .IA_64.unwind_info : { *(.IA_64.unwind_info*) }
    .IA_64.unwind : { *(.IA_64.unwind*) }
    .bss : {
        *(.bss*)
        *(COMMON)
    }
    .reloc ALIGN(0x1000) : {
        LONG((__efi_entry_descriptor - __image_base) & ~0xfff)
        LONG(16)
        SHORT(0xa000 | ((__efi_entry_descriptor - __image_base) & 0xfff))
        SHORT(0xa000 | ((__efi_entry_descriptor + 8 - __image_base) & 0xfff))
        SHORT(0xa000 |
              ((efi_entry_descriptor_reference - __image_base) & 0xfff))
        SHORT(0)
    }
    ASSERT(((__efi_entry_descriptor - __image_base) & ~0xfff) ==
           ((efi_entry_descriptor_reference - __image_base) & ~0xfff),
           "i2000-loader relocation targets must share one page")
}
