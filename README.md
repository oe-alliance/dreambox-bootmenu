# Boot menu for the Dreambox One and Two

Replaces the boot menu built into the Dreambox u-boot (`dm_bootmenu`) on boxes
with the GPT multiboot layout. It reads the same `/data/bootconfig.txt`, so
nothing new has to be configured, but it is not limited to eight entries, shows
the Dreambox logo with a scrolling list and is driven by the Dreambox remotes.

## How it hooks in

The Dreambox u-boot runs

    if ext4load mmc 1:6 ${loadaddr} /boot/autoexec.img; then autoscr ${loadaddr}; fi

before its own menu. On a GPT box partition 6 is the root file system of the
second slot. Images with the Dreambox flash scripts (openATV, Gemini,
Newnigma2, ...) build that `autoexec.img` with `update-autoexec` from all
`/etc/u-boot.scr.d/*.scr`, and they rebuild it whenever the video mode
changes. `bootmenu.scr` is one more of these snippets, so it ends up in every
rebuild of the image in slot 2.

The snippet loads `bootmenu.bin` from `/data/bootmenu/` and starts it with
`go`. The program shows the menu and hands the chosen entry back; the script
then puts it into `bootcmd` and lets u-boot continue. `dm_bootmenu` finds no
`loadaddr` and gives up without a menu, u-boot initialises the network (so
the kernel gets the real MAC address) and boots the chosen entry.

If `bootmenu.bin` is missing or returns nothing, the snippet does nothing and
the built-in menu comes up as before.

## Installing

From the feed on openATV:

    opkg install dreambox-bootmenu

On any other image with the Dreambox flash scripts (no package manager
needed), as root:

    curl -fsSL https://github.com/oe-alliance/dreambox-bootmenu/releases/latest/download/install.sh | sh
    wget -qO- https://github.com/oe-alliance/dreambox-bootmenu/releases/latest/download/install.sh | sh

`install.sh` fetches the files of the latest release into `/data/bootmenu`,
puts `bootmenu.scr` into the image in the second slot and rebuilds its
`autoexec.img`. It checks first that it runs on a Dreambox One or Two, that
`/data` is mounted, that the box has the GPT multiboot layout and that the
second slot holds an image. `--remove` (`| sh -s -- --remove`) takes the boot
menu out again, `--no-download` only sets up the second slot from the files
already in `/data/bootmenu`; the package uses that.

If the image in the second slot is flashed again, the boot menu is gone until
it is set up again. `install.sh` stays in `/data/bootmenu`, so this works from
any slot without a download, by hand or from a start script:

    /data/bootmenu/install.sh --no-download

## Files

| file | where it goes |
|---|---|
| `bootmenu.bin`, `bootlogo.bmp`, `bootmenu.scr`, `install.sh`, `NOTICE`, `LICENSE`, `LICENSE.fonts` | `/data/bootmenu/` |
| `bootmenu.scr` | also `/etc/u-boot.scr.d/` of the image in slot 2 |

## The program

A u-boot standalone program, started as

    go <load> <cfg> <cfglen> <result> [<fb> [<width> <height> [<logo>]]]

- `cfg`/`cfglen`: `bootconfig.txt`, already loaded by the script
- `result`: where the choice is written
- `fb`: the framebuffer, `0x${initrd_high}` as u-boot set it up for the logo
- `logo`: `bootlogo.bmp` as loaded by the script (RGB565, screen size); it is
  drawn only when the menu shows

The result is a magic (`0x4d454e55`), a zero word, the index and the length
of a text block with `bm_sel`, `bm_title`, `bm_cmd` and `bm_arg` for
`env import -t`. `bm_arg` is the `arg=` line without the literal
`${bootargs}` the built-in menu passes on.

Titles are UTF-8 (a byte order mark is skipped); the fonts cover ASCII,
Latin-1, Latin Extended-A and the extra characters of Windows-1252 (euro
sign, typographic quotes and dashes), anything else is drawn as `?`.

It honours `default=` and `timeout=` from `bootconfig.txt`; the countdown
runs down to 0 like the built-in menu. With `timeout=0` there is no logo and
no menu, the default entry starts right away (the built-in menu then rejects
the file and starts whatever u-boot has stored). OK held on the remote while
the box starts always brings the menu, without a countdown; a ring around
"OK" then stands where the countdown would be, as it does once up or down
stopped the countdown. The version shows below it.

Keys: up, down and OK on the Dreambox remotes (NEC, addresses `0x32` to
`0x35`), held keys repeat. On the serial console `u`, `d` and Return do the
same, and `q` or Ctrl+C leaves the menu without a choice, so the built-in
menu takes over. The console is also checked once before anything is drawn.

## Building

    make CROSS=aarch64-oe-linux-

The program uses no libc and no FP registers, keeps `x18` free (u-boot's
global data pointer) and has no `.data` or `.bss`, so the raw image can be
loaded and run as is at `0x03000000`. `bootmenu.scr` loads `bootconfig.txt`
to `0x03800000`, well above the program.

`fonts.h`, `fonts_bold.h` and `fonts_small.h` are generated from the Roboto
fonts in `fonts/` (Apache License 2.0, see `fonts/LICENSE`); `make fonts`
regenerates them and needs Pillow.

## Releases

The version is `BOOTMENU_VERSION` in `menu.c`. When it changes on master, a
workflow tags `v<version>` and publishes a release with the files above;
`install.sh` takes them from the latest release. The same files also come as
`dreambox-bootmenu-<version>.tar.gz`, which packages such as the OE recipe
use, so every installation runs the binary built by the release. Every push
is built and checked by the build workflow.

## License

Copyright (C) 2026 OE-Alliance. GPL-3.0, see `LICENSE`. The fonts in
`fonts/` and the glyph data generated from them are Apache License 2.0, see
`fonts/LICENSE`. `NOTICE` sums this up and is installed with `LICENSE` and
`LICENSE.fonts` into `/data/bootmenu`.
