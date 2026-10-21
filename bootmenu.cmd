# dreambox-bootmenu - https://github.com/oe-alliance/dreambox-bootmenu
# Copyright (C) 2026 OE-Alliance, GPL-3.0-only
#
# Built into bootmenu.img, which lies in /data/bootmenu next to bootmenu.bin
# and is run by the stub bootmenu.scr in the second slot. Script and program
# are always updated together this way.
# Without a video mode, e.g. a freshly flashed image that has not run yet,
# the bootlogo snippet sets up neither the OSD nor the output; do it with
# 1080p50 then, in the same order as the snippet. An empty value
# drops out as an argument, so it is compared with a prefix.
if test "x${outputmode}" = "x"; then setenv outputmode 1080p50hz; setenv hdmimode 1080p50hz; osd open; osd clear; bmp scale; vout output ${outputmode}; fi
setenv bm_logo 0x04000000
mw.l ${bm_logo} 0 1
ext4load mmc 1:4 ${bm_logo} /bootmenu/bootlogo.bmp
setenv bm_app 0x03000000
setenv bm_cfg 0x03800000
setenv bm_res 0x03810000
mw.l ${bm_res} 0 4
if ext4load mmc 1:4 ${bm_cfg} /bootconfig.txt; then setenv bm_len ${filesize}; if ext4load mmc 1:4 ${bm_app} /bootmenu/bootmenu.bin; then dcache flush; icache flush; go ${bm_app} ${bm_cfg} ${bm_len} ${bm_res} 0x${initrd_high} 500 2d0 ${bm_logo}; fi; fi
setexpr.l bm_magic *${bm_res}
if itest.l ${bm_magic} == 4d454e55; then setexpr bm_txt ${bm_res} + 10; env import -t ${bm_txt}; setenv bootargs ${bm_arg}; setenv bootcmd "setenv loadaddr 1080000; run bm_cmd"; setenv loadaddr; fi
