#!/bin/sh
#
# dreambox-bootmenu - https://github.com/oe-alliance/dreambox-bootmenu
# Copyright (C) 2026 OE-Alliance
# SPDX-License-Identifier: GPL-3.0-only
#
# Sets up the boot menu on a Dreambox One or Two with the GPT multiboot
# layout, also on images without a package for it:
#
#   curl -fsSL https://github.com/oe-alliance/dreambox-bootmenu/releases/latest/download/install.sh | sh
#   wget -qO- https://github.com/oe-alliance/dreambox-bootmenu/releases/latest/download/install.sh | sh
#
# Options (with "| sh -s -- <option>" when piped):
#   --no-download  only set up the second slot from the files already in
#                  /data/bootmenu, e.g. /data/bootmenu/install.sh --no-download
#                  after a new image in the second slot
#   --remove       take the boot menu out again, the built-in one comes back
#   --version <x>  install release <x> instead of the latest one, e.g. 1.1
#
# The files come from dreambox-bootmenu.tar.gz of the latest release, of the
# release given with --version, or of BOOTMENU_RELEASE when set.
#
# u-boot runs /boot/autoexec.img of partition 6, the second slot. Images with
# the Dreambox flash scripts build it from /etc/u-boot.scr.d/*.scr; the boot
# menu adds the stub bootmenu.scr there and rebuilds autoexec.img the same
# way. The stub only runs bootmenu.img from /data/bootmenu, so it never has to
# change; openATV images bring it along already.

RELEASES=https://github.com/oe-alliance/dreambox-bootmenu/releases
RELEASE=${BOOTMENU_RELEASE:-$RELEASES/latest/download}
TARGET=/data/bootmenu
ARCHIVE=dreambox-bootmenu.tar.gz
FILES="bootmenu.bin bootlogo.bmp bootmenu.scr install.sh NOTICE LICENSE LICENSE.fonts"
# Releases before 1.2 have no bootmenu.img, their bootmenu.scr is the whole script.
OPTIONAL="bootmenu.img"
SLOT2=/dev/mmcblk0p6

say() {
	echo "dreambox-bootmenu: $*"
}

fail() {
	say "$*"
	exit 1
}

download() {
	if command -v curl >/dev/null 2>&1; then
		curl -fsSL -o "$2" "$1"
	elif command -v wget >/dev/null 2>&1; then
		wget -q -O "$2" "$1"
	else
		return 1
	fi
}

# Runs with the root of the second slot in $1, empty when that is "/".
setup_slot() {
	root=$1
	if [ "$MODE" = remove ]; then
		# The stub of an image package stays; without /data/bootmenu it does nothing.
		grep -qs "^/etc/u-boot.scr.d/bootmenu.scr" "$root"/var/lib/opkg/info/*.list && return 0
		rm -f "$root/etc/u-boot.scr.d/bootmenu.scr"
	elif cmp -s "$TARGET/bootmenu.scr" "$root/etc/u-boot.scr.d/bootmenu.scr" && grep -q /bootmenu/ "$root/boot/autoexec.img" 2>/dev/null; then
		return 0
	else
		cp "$TARGET/bootmenu.scr" "$root/etc/u-boot.scr.d/bootmenu.scr" || return 1
	fi
	if [ -z "$root" ] && command -v update-autoexec >/dev/null 2>&1; then
		update-autoexec
		return $?
	fi
	script=$(mktemp) || return 1
	LC_ALL=C grep -h "^[a-zA-Z0-9]" "$root"/etc/u-boot.scr.d/*.scr | sed "s/mmc 1:6/mmc 1:5/g" > "$script"
	mkimage -A arm64 -O linux -T script -C none -n autoexec -d "$script" "$root/boot/autoexec.img.tmp" >/dev/null &&
		mv "$root/boot/autoexec.img.tmp" "$root/boot/autoexec.img"
	status=$?
	rm -f "$script"
	return $status
}

MODE=install
while [ $# -gt 0 ]; do
	case "$1" in
		--no-download) MODE=local ;;
		--remove) MODE=remove ;;
		--version)
			[ -n "$2" ] || fail "--version needs a version, e.g. --version 1.1"
			RELEASE=$RELEASES/download/v${2#v}
			shift
			;;
		*) fail "unknown option $1" ;;
	esac
	shift
done

[ "$(id -u)" -eq 0 ] || fail "must run as root."
case "$(cat /proc/stb/info/model 2>/dev/null)" in
	one|two|dreamone|dreamtwo) ;;
	*) fail "only for the Dreambox One and Two." ;;
esac
grep -q " /data " /proc/mounts || fail "/data is not mounted."
[ "$(dd if=/dev/mmcblk0 bs=1 skip=512 count=8 2>/dev/null)" = "EFI PART" ] || fail "no GPT on the eMMC, nothing to do."
grep -q "^PARTNAME=dreambox-rootfs1$" /sys/class/block/mmcblk0p6/uevent 2>/dev/null || fail "no GPT multiboot layout, nothing to do."
[ "$MODE" = remove ] || command -v mkimage >/dev/null 2>&1 || fail "mkimage is missing (package u-boot-mkimage or u-boot-tools-mkimage)."

if [ "$MODE" = install ]; then
	mkdir -p "$TARGET" || fail "cannot create $TARGET."
	# Unpacked next to the target, so the files move over on the same file system.
	work=$(mktemp -d "$TARGET.XXXXXX") || fail "cannot create a work directory in /data."
	if ! download "$RELEASE/$ARCHIVE" "$work/$ARCHIVE" || ! tar -xzf "$work/$ARCHIVE" -C "$work"; then
		rm -rf "$work"
		fail "cannot download or unpack $RELEASE/$ARCHIVE (curl or wget with https needed)."
	fi
	for file in $FILES; do
		[ -f "$work/dreambox-bootmenu/$file" ] && continue
		rm -rf "$work"
		fail "$file is missing in $ARCHIVE."
	done
	for file in $FILES; do
		mv "$work/dreambox-bootmenu/$file" "$TARGET/$file"
	done
	for file in $OPTIONAL; do
		if [ -f "$work/dreambox-bootmenu/$file" ]; then
			mv "$work/dreambox-bootmenu/$file" "$TARGET/$file"
		else
			rm -f "$TARGET/$file"
		fi
	done
	rm -rf "$work"
	chmod 0755 "$TARGET/install.sh"
	say "files installed in $TARGET."
fi
if [ "$MODE" != remove ]; then
	for file in bootmenu.bin bootmenu.scr; do
		[ -f "$TARGET/$file" ] || fail "$TARGET/$file is missing."
	done
fi

if grep -q "root=$SLOT2 " /proc/cmdline; then
	setup_slot "" || fail "cannot set up the second slot."
else
	mnt=$(mktemp -d) || fail "cannot create a mount point."
	if ! mount "$SLOT2" "$mnt" 2>/dev/null; then
		rmdir "$mnt"
		fail "the second slot is empty, the boot menu needs an image there."
	fi
	if [ -d "$mnt/boot" ] && [ -d "$mnt/etc/u-boot.scr.d" ]; then
		setup_slot "$mnt"
		status=$?
	else
		status=2
	fi
	umount "$mnt"
	rmdir "$mnt"
	[ $status -eq 2 ] && fail "no image with the Dreambox flash scripts in the second slot."
	[ $status -eq 0 ] || fail "cannot set up the second slot."
fi

if [ "$MODE" = remove ]; then
	rm -rf "$TARGET"
	say "removed, the built-in boot menu is back."
else
	say "set up, the boot menu comes with the next start."
fi
