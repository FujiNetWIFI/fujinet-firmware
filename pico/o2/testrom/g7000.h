; This file contains definitions I use in my programs.

; $Id: g7000.h 594 2006-12-08 13:59:35Z sgust $

; Version 1.3

; Copyright (C) 1997-2006 by Soeren Gust, sgust@ithh.informationstheater.de

; Permission is hereby granted, free of charge, to any person obtaining a copy
; of this software and associated documentation files (the "Software"), to deal
; in the Software without restriction, including without limitation the rights
; to use, copy, modify, merge, publish, distribute, sublicense, and/or sell
; copies of the Software, and to permit persons to whom the Software is
; furnished to do so, subject to the following conditions:

; The above copyright notice and this permission notice shall be included in
; all copies or substantial portions of the Software.

; THE SOFTWARE IS PROVIDED "AS IS", WITHOUT WARRANTY OF ANY KIND, EXPRESS OR
; IMPLIED, INCLUDING BUT NOT LIMITED TO THE WARRANTIES OF MERCHANTABILITY,
; FITNESS FOR A PARTICULAR PURPOSE AND NONINFRINGEMENT.  IN NO EVENT SHALL THE
; AUTHORS OR COPYRIGHT HOLDERS BE LIABLE FOR ANY CLAIM, DAMAGES OR OTHER
; LIABILITY, WHETHER IN AN ACTION OF CONTRACT, TORT OR OTHERWISE, ARISING FROM,
; OUT OF OR IN CONNECTION WITH THE SOFTWARE OR THE USE OR OTHER DEALINGS IN
; THE SOFTWARE.

; NOTE: Since version 0.8.8 I have changed the license conditions. The former
; GNU Public License did not really make sense since this is just an include
; file and does not generate code on its own.

; You can always get the latest version at http://soeren.informationstheater.de

; BIOS routines for Videopac G7000
irq		equ	0009h
irqend		equ	0014h
vsyncirq	equ	001Ah
soundirq	equ	0044h
parsesnd	equ	004Bh
copyregs	equ	0089h	; internal
readkey		equ	00B0h
readkey_plus	equ	00BCh	; entry point for readkey on VP+ G7400
vdcenable	equ	00E7h
extramenable	equ	00ECh
init		equ	00F1h
gfxoff		equ	011Ch
gfxon		equ	0127h
tableend	equ	0132h
waitforkey	equ	013Dh
calcchar23	equ	014Bh
clearchar	equ	016Bh
waitvsync	equ	0176h
tablebcdbyte	equ	017Ch
tableprintchar	equ	0197h
playsound	equ	01A2h
doclock		equ	01B0h
tablechar23	equ	022Ch
tablebcdnibble	equ	0229h
tableput2	equ	0235h
initclock	equ	023Ah
putchar23	equ	0261h
bittest		equ	026Ah	; not available on Videopac+ G7400
bitclear	equ	0280h	; not available on Videopac+ G7400
bitset		equ	028Ah	; not available on Videopac+ G7400
random		equ	0293h	; not available on Videopac+ G7400
nibblemixer	equ	02A4h	; not available on Videopac+ G7400
selectgame	equ	02C3h
bank02		equ	037Fh
bank01		equ	0383h
bank0		equ	0387h
bank3		equ	038Bh
getjoystick	equ	038Fh
getjoystick_p17	equ	0395h	; entry point for getjoystick when using P17
decodejoystick	equ	03B1h
divide		equ	03DDh
multiply	equ	03CFh
printchar	equ	03EAh

; the vdc registers
vdc_spr0_ctrl	equ	000h
vdc_spr1_ctrl	equ	004h
vdc_spr2_ctrl	equ	008h
vdc_spr3_ctrl	equ	00Ch

vdc_char0	equ	010h
vdc_char1	equ	014h
vdc_char2	equ	018h
vdc_char3	equ	01Ch
vdc_char4	equ	020h
vdc_char5	equ	024h
vdc_char6	equ	028h
vdc_char7	equ	02Ch
vdc_char8	equ	030h
vdc_char9	equ	034h
vdc_chara	equ	038h
vdc_charb	equ	03Ch

vdc_quad0	equ	040h
vdc_quad1	equ	050h
vdc_quad2	equ	060h
vdc_quad3	equ	070h

vdc_spr0_shape	equ	080h
vdc_spr1_shape	equ	088h
vdc_spr2_shape	equ	090h
vdc_spr3_shape	equ	098h

vdc_control	equ	0a0h
vdc_status	equ	0a1h
vdc_collision	equ	0a2h
vdc_color	equ	0a3h
vdc_scanline	equ	0a4h
vdc_scanrow	equ	0a5h
;vdc_unknown	equ	0a6h
vdc_sound0	equ	0a7h
vdc_sound1	equ	0a8h
vdc_sound2	equ	0a9h
vdc_soundctrl	equ	0aah

vdc_gridh0	equ	0C0h
vdc_gridh1	equ	0C1h
vdc_gridh2	equ	0C2h
vdc_gridh3	equ	0C3h
vdc_gridh4	equ	0C4h
vdc_gridh5	equ	0C5h
vdc_gridh6	equ	0C6h
vdc_gridh7	equ	0C7h
vdc_gridh8	equ	0C8h

vdc_gridi0	equ	0D0h
vdc_gridi1	equ	0D1h
vdc_gridi2	equ	0D2h
vdc_gridi3	equ	0D3h
vdc_gridi4	equ	0D4h
vdc_gridi5	equ	0D5h
vdc_gridi6	equ	0D6h
vdc_gridi7	equ	0D7h
vdc_gridi8	equ	0D8h

vdc_gridv0	equ	0E0h
vdc_gridv1	equ	0E1h
vdc_gridv2	equ	0E2h
vdc_gridv3	equ	0E3h
vdc_gridv4	equ	0E4h
vdc_gridv5	equ	0E5h
vdc_gridv6	equ	0E6h
vdc_gridv7	equ	0E7h
vdc_gridv8	equ	0E8h
vdc_gridv9	equ	0E9h

; the bits in the vdc_control
vdc_ctrl_hint	equ	001h
vdc_ctrl_beam	equ	002h
vdc_ctrl_sint	equ	004h
vdc_ctrl_grid	equ	008h
vdc_ctrl_ovrlay	equ	010h
vdc_ctrl_fore	equ	020h
vdc_ctrl_dot	equ	040h
vdc_ctrl_fill	equ	080h

; the bits in vdc_status
vdc_stat_hblank	equ	001h
vdc_stat_pstrb	equ	002h
vdc_stat_sound	equ	004h
vdc_stat_vblank	equ	008h
vdc_stat_bit4	equ	010h
vdc_stat_bit5	equ	020h
vdc_stat_ovrlay	equ	040h	; unused in G7000/O^2
vdc_stat_chrlap	equ	080h

; the bits in vdc_collision
vdc_coll_spr0	equ	001h
vdc_coll_spr1	equ	002h
vdc_coll_spr2	equ	004h
vdc_coll_spr3	equ	008h
vdc_coll_vgrd	equ	010h
vdc_coll_hgrd	equ	020h
vdc_coll_ext	equ	040h	; only used on Videopac+ G7400
vdc_coll_char	equ	080h

; the bits in vdc_soundctrl
vdc_sound_noise	equ	010h
vdc_sound_freq	equ	020h
vdc_sound_loop	equ	040h
vdc_sound_enab	equ	080h

; the names match the colors on my PAL Videopac G7000

; the colors for characters
col_chr_black	equ	00h << 1
col_chr_red	equ	01h << 1
col_chr_green	equ	02h << 1
col_chr_yellow	equ	03h << 1
col_chr_blue	equ	04h << 1
col_chr_violet	equ	05h << 1
col_chr_cyan	equ	06h << 1
col_chr_white	equ	07h << 1

; sprite control byte 3
spr_evenshift	equ	02h
spr_double	equ	04h
; colors for sprites
col_spr_black	equ	00h << 3
col_spr_red	equ	01h << 3
col_spr_green	equ	02h << 3
col_spr_yellow	equ	03h << 3
col_spr_blue	equ	04h << 3
col_spr_violet	equ	05h << 3
col_spr_cyan	equ	06h << 3
col_spr_white	equ	07h << 3

; the colors for the grid
col_grd_black	equ	00h
col_grd_blue	equ	01h
col_grd_green	equ	02h
col_grd_cyan	equ	03h
col_grd_red	equ	04h
col_grd_violet	equ	05h
col_grd_yellow	equ	06h
col_grd_white	equ	07h

; colors for the grid
col_bck_black	equ	00h << 3
col_bck_blue	equ	01h << 3
col_bck_green	equ	02h << 3
col_bck_cyan	equ	03h << 3
col_bck_red	equ	04h << 3
col_bck_violet	equ	05h << 3
col_bck_yellow	equ	06h << 3
col_bck_white	equ	07h << 3
; use this to make the grid brighter
col_grd_lum	equ	040h

_ARROW equ 36h
_A equ 20h
_B equ 25h
_C equ 23h
_D equ 1Ah
_E equ 12h
_F equ 1Bh
_G equ 1Ch
_H equ 1Dh
_I equ 16h
_J equ 1Eh
_K equ 1Fh
_L equ 0Eh
_M equ 26h
_N equ 2Dh
_O equ 17h
_P equ 0Fh
_Q equ 18h
_R equ 13h
_S equ 19h
_T equ 14h
_U equ 15h
_V equ 24h
_X equ 22h
_W equ 11h
_Y equ 2Ch
_Z equ 21h
_0 equ 0h
_1 equ 1h
_2 equ 2h
_3 equ 3h
_4 equ 4h
_5 equ 5h
_6 equ 6h
_7 equ 7h
_8 equ 8h
_9 equ 9h
_10 equ 30h

; the locations in internal ram
iram_collision	equ	03Dh
iram_clock	equ	03Eh
iram_irqctrl	equ	03Fh

; some bits in the iram
; iram_clock
clock_stop	equ	080h
clock_forward	equ	040h
; iram_irqctrl
irq_table	equ	080h
irq_sound	equ	040h

; the locations in external ram
eram_minutes	equ	001h
eram_seconds	equ	002h

; the builtin tunes
tune_beep_error	equ	028h
tune_explode	equ	02Eh
tune_alarm	equ	03Ch
tune_select	equ	04Ah
tune_keyclick	equ	056h
tune_buzz	equ	05Ah
tune_select2	equ	05Eh
tune_shoot	equ	06Ah

; BIOS routines for Videopac+ G7400
plusready	equ	027Dh
plusloadr	equ	0283h
pluscmd		equ	0288h
plusdata	equ	028Ch
plushide	equ	0296h
plusmode	equ	0299h
plusenable	equ	02A1h
plusstart	equ	02ABh
plusselectgame	equ	02C2h

; registers of the EF9340/41
vpp_ta_wr	equ	0
vpp_tb_wr	equ	1
vpp_ta_cmd	equ	2
vpp_tb_cmd	equ	3
vpp_ta_rd	equ	4
vpp_tb_rd	equ	5
vpp_busy	equ	6
; 7 is illegal

; commandbytes for the Videopac+ G7400
plus_cmd_brow	equ	000h	; begin row
plus_cmd_loady	equ	020h	; load Y
plus_cmd_loadx	equ	040h	; load X
plus_cmd_incc	equ	060h	; inc C
plus_cmd_loadm	equ	080h	; load M
plus_cmd_loadr	equ	0A0h	; load R
plus_cmd_loady0	equ	0C0h	; load Y0

; parallel attributes for the Videopac+ G7400
; foreground color
col_plus_black	equ	0
col_plus_red	equ	1
col_plus_green	equ	2
col_plus_yellow	equ	3
col_plus_blue	equ	4
col_plus_violet	equ	5
col_plus_cyan	equ	6
col_plus_white	equ	7
; background color, only parallel for block gfx
col_pbck_black	equ	0 << 4
col_pbck_red	equ	1 << 4
col_pbck_green	equ	2 << 4
col_pbck_yellow	equ	3 << 4
col_pbck_blue	equ	4 << 4
col_pbck_violet	equ	5 << 4
col_pbck_cyan	equ	6 << 4
col_pbck_white	equ	7 << 4
; other parallel attributes
col_patr_blck	equ	080h	; block gfx
col_patr_invrt	equ	040h	; invert
col_patr_dwdth	equ	020h	; double width
col_patr_dhght	equ	010h	; double height
col_patr_stable	equ	008h	; do not blink

; serial attributes
; other serial attributes
col_satr_enable	equ	080h	; enable serial attributes
col_satr_line	equ	004h	; underline
col_satr_box	equ	002h	; boxing mode
col_satr_conc	equ	001h	; conceal mode

; block gfx
plus_blck_full	equ	040h	; use full blocks

; parameter for plus_cmd_loadm
plus_loadm_wr	equ	000h	; write
plus_loadm_rd	equ	020h	; read
plus_loadm_wrni	equ	040h	; write without inc
plus_loadm_rdni	equ	060h	; read without inc
plus_loadm_wrsl	equ	080h	; write slice
plus_loadm_rdsl	equ	0A0h	; read slice

; parameter for plus_cmd_loadr
plus_loadr_blnk	equ	080h	; enable blink
plus_loadr_tt	equ	040h	; 50/60 Hz, handled by plusloadr
plus_loadr_tl	equ	020h	; monitor mode, leave 0
plus_loadr_crsr	equ	010h	; display cursor
plus_loadr_srow	equ	008h	; service row (first line)
plus_loadr_conc	equ	004h	; show concealed
plus_loadr_box	equ	002h	; box mode enable
plus_loadr_dspl	equ	001h	; show display

; parameter for plus_cmd_loady0
plus_loady0_zom	equ	020h	; global double height

; registers for the MegaCART/FlashCART, mirrored every 4 bytes
ereg_codebank	equ	080h	; code bank
ereg_databank	equ	081h	; data bank
ereg_io_out	equ	082h	; output port
ereg_io_in	equ	083h	; input port

; bits in ereg_io_out
eout_tx		equ	001h	; send data from FlashCART to PC
eout_eecs	equ	002h	; chip select for serial EEPROM
eout_eeclk	equ	004h	; clock for serial EEPROM
eout_eedi	equ	008h	; data for serial EEPROM

; bits in ereg_io_in
ein_eedo	equ	001h	; data from serial EEPROM
