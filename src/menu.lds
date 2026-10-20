/*
 * dreambox-bootmenu - https://github.com/oe-alliance/dreambox-bootmenu
 * Copyright (C) 2026 OE-Alliance
 * SPDX-License-Identifier: GPL-3.0-only
 *
 * A raw image for "go": entry at the load address, no data, no bss.
 */

ENTRY(menu_main)

SECTIONS
{
	. = LOAD_ADDR;
	.text : {
		KEEP(*(.text.entry))
		*(.text .text.*)
	}
	.rodata : { *(.rodata .rodata.*) }
	.data : { *(.data .data.*) }
	.bss : { *(.bss .bss.* COMMON) }
	/DISCARD/ : { *(.comment) *(.note*) *(.eh_frame*) }
}

ASSERT(SIZEOF(.data) == 0, "menu: no .data allowed, the image is copied as is")
ASSERT(SIZEOF(.bss) == 0, "menu: no .bss allowed, nothing clears it")
