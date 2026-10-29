# dreambox-bootmenu - https://github.com/oe-alliance/dreambox-bootmenu
# Copyright (C) 2026 OE-Alliance, GPL-3.0-only
#
# Built into bootmenu.img, which lies in /data/bootmenu next to bootmenu.bin
# and is run by the stub bootmenu.scr in the second slot. Script and program
# are always updated together this way.
# The menu is drawn in 1080p50 like the built-in one, the mode every TV takes,
# and in RGB, which every HDMI TV has to take; some show nothing of the YCbCr
# u-boot sends otherwise. The image in the second slot may have set no mode or
# another one, e.g. 1080p60, and the colour space only changes with the mode.
# An empty value drops out as an argument, so it is compared with a prefix.
setenv bm_vout "setenv outputmode 1080p50hz; setenv hdmimode 1080p50hz; setenv colorattribute rgb,8bit; osd open; osd clear; bmp scale; vout output 1080p50hz"
test "x${outputmode} ${colorattribute}" = "x1080p50hz rgb,8bit" || run bm_vout
setenv bm_logo 0x04000000
mw.l ${bm_logo} 0 1
ext4load mmc 1:4 ${bm_logo} /bootmenu/bootlogo.bmp
setenv bm_app 0x03000000
setenv bm_cfg 0x03800000
setenv bm_res 0x03810000
mw.l ${bm_res} 0 4
# Without bootconfig.txt, or with an empty one, the copy install.sh keeps is
# used; without that too, the program brings fallback entries.
setenv bm_len 0
setenv bm_src 0
if ext4load mmc 1:4 ${bm_cfg} /bootconfig.txt; then setenv bm_len ${filesize}; fi
if test "x${bm_len}" = "x0"; then if ext4load mmc 1:4 ${bm_cfg} /bootmenu/bootconfig.bak; then setenv bm_len ${filesize}; setenv bm_src 1; fi; fi
# The entry started last lives in the last sector of u-boot-env: the program
# finds it in the GPT, the menu reads and updates it, the script writes it
# back only when the menu asks for it.
setenv bm_gpt 0x03840000
setenv bm_rec 0
setenv bm_recwrite 0
setenv bm_recblk
setenv bm_datablk
setenv bm_ok 0
if ext4load mmc 1:4 ${bm_app} /bootmenu/bootmenu.bin; then setenv bm_ok 1; dcache flush; icache flush; fi
if test "x${bm_ok}" = "x1"; then if mmc dev 1; then if mmc read ${bm_gpt} 0 22; then go ${bm_app} gpt ${bm_gpt} ${bm_res}; fi; fi; fi
setexpr.l bm_magic *${bm_res}
if itest.l ${bm_magic} == 4d454e55; then setexpr bm_txt ${bm_res} + 10; env import -t ${bm_txt}; if mmc read 0x03830000 ${bm_recblk} 1; then setenv bm_rec 0x03830000; fi; fi
# Inode and ctime of bootconfig.txt tell the menu whether anything wrote the
# file since the record, even the same default= again. The program walks
# dreambox-data in four steps, each names the blocks the script reads for
# the next; the last hands back the stamp as bm_st. Read only.
setenv bm_ext 0x03850000
setenv bm_blk 0x03860000
setenv bm_st 0
setenv bm_step 1
setenv bm_ino 0
setenv bm_rn 0
if test "x${bm_rec}" != "x0"; then if test "x${bm_datablk}" != "x"; then if mmc read ${bm_ext} ${bm_datablk} 40; then setenv bm_rn 1; fi; fi; fi
setenv bm_x 0
if test "x${bm_rn}" != "x0"; then mw.l ${bm_res} 0 4; go ${bm_app} ext4 ${bm_ext} ${bm_blk} ${bm_res} ${bm_datablk} ${bm_step} ${bm_ino}; setenv bm_rn 0; setexpr.l bm_magic *${bm_res}; if itest.l ${bm_magic} == 4d454e55; then setexpr bm_txt ${bm_res} + 10; env import -t ${bm_txt}; fi; setenv bm_x ${bm_rn}; setenv bm_rn 0; if test "x${bm_x}" != "x0"; then mw.l ${bm_blk} 0 4000; if mmc read ${bm_blk} ${bm_rd} ${bm_x}; then setenv bm_rn ${bm_x}; fi; fi; fi
if test "x${bm_rn}" != "x0"; then mw.l ${bm_res} 0 4; go ${bm_app} ext4 ${bm_ext} ${bm_blk} ${bm_res} ${bm_datablk} ${bm_step} ${bm_ino}; setenv bm_rn 0; setexpr.l bm_magic *${bm_res}; if itest.l ${bm_magic} == 4d454e55; then setexpr bm_txt ${bm_res} + 10; env import -t ${bm_txt}; fi; setenv bm_x ${bm_rn}; setenv bm_rn 0; if test "x${bm_x}" != "x0"; then mw.l ${bm_blk} 0 4000; if mmc read ${bm_blk} ${bm_rd} ${bm_x}; then setenv bm_rn ${bm_x}; fi; fi; fi
if test "x${bm_rn}" != "x0"; then mw.l ${bm_res} 0 4; go ${bm_app} ext4 ${bm_ext} ${bm_blk} ${bm_res} ${bm_datablk} ${bm_step} ${bm_ino}; setenv bm_rn 0; setexpr.l bm_magic *${bm_res}; if itest.l ${bm_magic} == 4d454e55; then setexpr bm_txt ${bm_res} + 10; env import -t ${bm_txt}; fi; setenv bm_x ${bm_rn}; setenv bm_rn 0; if test "x${bm_x}" != "x0"; then mw.l ${bm_blk} 0 4000; if mmc read ${bm_blk} ${bm_rd} ${bm_x}; then setenv bm_rn ${bm_x}; fi; fi; fi
if test "x${bm_rn}" != "x0"; then mw.l ${bm_res} 0 4; go ${bm_app} ext4 ${bm_ext} ${bm_blk} ${bm_res} ${bm_datablk} ${bm_step} ${bm_ino}; setexpr.l bm_magic *${bm_res}; if itest.l ${bm_magic} == 4d454e55; then setexpr bm_txt ${bm_res} + 10; env import -t ${bm_txt}; fi; fi
mw.l ${bm_res} 0 4
if test "x${bm_ok}" = "x1"; then go ${bm_app} ${bm_cfg} ${bm_len} ${bm_res} 0x${initrd_high} 500 2d0 ${bm_logo} ${bm_src} ${bm_rec} ${bm_st} ${outputmode}; fi
setexpr.l bm_magic *${bm_res}
if itest.l ${bm_magic} == 4d454e55; then setexpr bm_txt ${bm_res} + 10; env import -t ${bm_txt}; setenv bootargs ${bm_arg}; setenv bootcmd "setenv loadaddr 1080000; run bm_cmd"; setenv loadaddr; if test "x${bm_recwrite}" = "x1"; then mmc write ${bm_rec} ${bm_recblk} 1; fi; fi
