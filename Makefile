# dreambox-bootmenu - https://github.com/oe-alliance/dreambox-bootmenu
# Copyright (C) 2026 OE-Alliance
# SPDX-License-Identifier: GPL-3.0-only

CROSS		?= aarch64-oe-linux-
LOAD_ADDR	?= 0x03000000

ifeq ($(origin CC),default)
CC		= $(CROSS)gcc
endif
OBJCOPY		?= $(CROSS)objcopy
PYTHON		?= python3
MKIMAGE		?= mkimage
SRC		= src

# A raw u-boot standalone program: no libc, no FP registers, x18 belongs to
# u-boot (gd), and the image must be copied as is (no .data, no .bss).
MENU_CFLAGS	= -Os -Wall -Wextra -ffreestanding -fno-builtin -nostdlib \
		  -fno-pic -fno-pie -fno-stack-protector -mgeneral-regs-only \
		  -mstrict-align -ffixed-x18 -fno-asynchronous-unwind-tables \
		  -fno-unwind-tables -ffunction-sections -fdata-sections \
		  -Wno-unused-function
MENU_LDFLAGS	= -nostdlib -static -no-pie -Wl,--build-id=none -Wl,--gc-sections \
		  -Wl,-T,$(SRC)/menu.lds -Wl,--defsym,LOAD_ADDR=$(LOAD_ADDR)

all: bootmenu.bin bootmenu.img

bootmenu.elf: $(addprefix $(SRC)/,menu.c gfx.h styles.h fonts.h fonts_bold.h fonts_small.h menu.lds)
	$(CC) $(MENU_CFLAGS) $(MENU_LDFLAGS) -o $@ $(SRC)/menu.c -lgcc

bootmenu.bin: bootmenu.elf
	$(OBJCOPY) -O binary $< $@
	@echo "$@: load address $(LOAD_ADDR), size $$(stat -c %s $@) bytes"

# The script the stub in the second slot runs, without the comments like
# update-autoexec builds autoexec.img.
bootmenu.img: bootmenu.cmd
	LC_ALL=C grep -h "^[a-zA-Z0-9]" $< > $@.txt
	$(MKIMAGE) -A arm64 -O linux -T script -C none -n bootmenu -d $@.txt $@ >/dev/null
	rm -f $@.txt

# The font headers are checked in; this regenerates them (needs Pillow).
fonts:
	$(PYTHON) mkfont.py fonts/Roboto-Regular.ttf $(SRC)/fonts.h font_sel:40 font_list:26 font_far:22 font_tiny:18
	$(PYTHON) mkfont.py --ascii fonts/Roboto-Bold.ttf $(SRC)/fonts_bold.h font_head:22
	$(PYTHON) mkfont.py --ascii fonts/Roboto-Regular.ttf $(SRC)/fonts_small.h font_micro:14

clean:
	rm -f bootmenu.elf bootmenu.bin bootmenu.img

.PHONY: all fonts clean
