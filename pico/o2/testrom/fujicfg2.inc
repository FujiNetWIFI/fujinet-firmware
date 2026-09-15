; fujicfg2.inc -- the browser's transaction and drawing helpers.
;
; Included AFTER fujilib.inc, which ends with the character map at $E00, so the
; location counter is sitting in the data area by then -- hence the explicit org
; back into the low bank. Everything here must stay below $800: see the banner
; in fujilib.inc for why.

    org 640h

; get_hosts: READ_HOST_SLOTS. The reply is 8 records of 32 bytes = 256, which
; is the one command here that needs both reply slices. Returns A = reply cmd.
get_hosts:
    mov a,#P1_SEL_XRAM
    outl p1,a
    mov r2,#0
    mov r3,#0
    mov a,#FUJICMD_READ_HOST_SLOTS
    call fn_begin
    call fn_fire
    ret

; mount_host: MOUNT_HOST(V_HOST). Returns A = reply cmd.
mount_host:
    mov a,#P1_SEL_XRAM
    outl p1,a
    mov r2,#1
    mov r3,#0
    mov a,#FUJICMD_MOUNT_HOST
    call fn_begin
    mov r0,#V_HOST
    mov a,@r0
    call fn_par1
    call fn_fire
    ret

; open_dir: OPEN_DIRECTORY(V_HOST) on "/". The payload is "path\0filter\0"
; NUL-padded to exactly 256 bytes; the firmware reads it with transaction_get()
; into a fixed buffer and fails a short read. Returns A = reply cmd.
open_dir:
    mov a,#P1_SEL_XRAM
    outl p1,a
    mov r2,#1
    mov r3,#1                   ; txlen = $0100
    mov a,#FUJICMD_OPEN_DIRECTORY
    call fn_begin
    mov r0,#V_HOST
    mov a,@r0
    call fn_par1
    call clear_name             ; the directory path is just "/"
    call push_name256
    call fn_fire
    ret

; read_entry: SET_DIRECTORY_POSITION(V_POS) then READ_DIR_ENTRY, copying the
; name into V_NAME padded with spaces. Returns A = 1 if this was the EOF marker
; (two $7F bytes), else 0.
read_entry:
    mov a,#P1_SEL_XRAM
    outl p1,a
    mov r2,#1
    mov r3,#0
    mov a,#FUJICMD_SET_DIRECTORY_POSITION
    call fn_begin
    mov r0,#V_POS
    mov a,@r0
    call fn_par1
    call fn_fire

    mov r2,#2
    mov r3,#0
    mov a,#FUJICMD_READ_DIR_ENTRY
    call fn_begin
    mov a,#NAMELEN
    call fn_par1
    clr a                       ; no "additional details"
    call fn_par1
    call fn_fire

    mov a,#FN_R_DATA&0FFh       ; EOF is exactly two $7F bytes
    call fn_read
    xrl a,#07Fh
    jnz re_copy
    mov a,#(FN_R_DATA+1)&0FFh
    call fn_read
    xrl a,#07Fh
    jnz re_copy
    mov a,#1
    ret

re_copy:
    mov r0,#V_NAME
    mov r1,#FN_R_DATA&0FFh
    mov r2,#NAMELEN
    mov r4,#1                   ; 1 = still copying, 0 = padding with spaces
re_l:
    mov a,r4
    jz re_pad
    mov a,r1
    call fn_read
    jnz re_put
    mov r4,#0
re_pad:
    mov a,#' '
re_put:
    mov @r0,a
    inc r0
    inc r1
    djnz r2,re_l
    clr a
    ret

; boot_entry: SET_DEVICE_FULLPATH("/" + the selected entry) then MOUNT_IMAGE.
; Returns A = reply cmd.
;
; The entry is re-read at full length first. READ_DIR_ENTRY crunches the name to
; the maxlen it is given, so the 12 characters the browser displays are not a
; filename -- "K.C. Munchkin (USA).bin" comes back as "K.C....bin" and would
; never open.
boot_entry:
    mov a,#P1_SEL_XRAM
    outl p1,a
    mov r2,#1
    mov r3,#0
    mov a,#FUJICMD_SET_DIRECTORY_POSITION
    call fn_begin
    mov r0,#V_POS
    mov a,@r0
    call fn_par1
    call fn_fire
    mov r2,#2
    mov r3,#0
    mov a,#FUJICMD_READ_DIR_ENTRY
    call fn_begin
    mov a,#PATHLEN              ; full length, not the display width
    call fn_par1
    clr a
    call fn_par1
    call fn_fire
    xrl a,#FUJICMD_ACK
    jz be_have_name             ; invert-and-JMP: the conditional reaches only
    jmp be_out                  ; within its own page, JMP reaches the bank
be_have_name:

    mov r2,#3
    mov r3,#1
    mov a,#FUJICMD_SET_DEVICE_FULLPATH
    call fn_begin
    mov a,#DEVSLOT
    call fn_par1
    mov r0,#V_HOST
    mov a,@r0
    call fn_par1
    mov a,#MODE_READ
    call fn_par1
    call push_entry256
    call fn_fire
    xrl a,#FUJICMD_ACK
    jz be_have_path
    jmp be_out
be_have_path:
    mov r2,#2
    mov r3,#0
    mov a,#FUJICMD_MOUNT_IMAGE
    call fn_begin
    mov a,#DEVSLOT
    call fn_par1
    mov a,#MODE_READ
    call fn_par1
    call fn_fire
    ret
be_out:
    clr a
    ret

; push_entry256: '/' then the directory entry sitting in the reply window,
; zero-padded to 256. Streamed straight out of the mailbox page rather than
; through RAM -- the console has 64 bytes of internal RAM and no room for a
; full-length filename. PATHLEN keeps the name inside the first reply slice, so
; no slice switching is needed. P1 = P1_SEL_XRAM.
push_entry256:
    mov a,#'/'
    call fn_push
    mov r1,#FN_R_DATA&0FFh
    mov r2,#0FFh
    mov r5,#1
pe_l:
    mov a,r5
    jz pe_zero
    mov a,r1
    call fn_read
    jnz pe_put
    mov r5,#0
pe_zero:
    clr a
pe_put:
    call fn_push
    inc r1
    djnz r2,pe_l
    ret

; clear_name: fill V_NAME with spaces, which push_name256 treats as "nothing
; after the slash" -- that is how the root directory path is built.
clear_name:
    mov r0,#V_NAME
    mov r2,#NAMELEN
cn_l:
    mov a,#' '
    mov @r0,a
    inc r0
    djnz r2,cn_l
    ret

; push_name256: '/' then V_NAME with its trailing spaces trimmed, zero-padded
; to 256. P1 = P1_SEL_XRAM.
push_name256:
    mov a,#'/'
    call fn_push
    mov r0,#V_NAME
    mov r2,#NAMELEN
    mov r5,#1                   ; still inside the name
pn_l:
    mov a,r5
    jz pn_skip
    mov a,@r0
    xrl a,#' '
    jz pn_stop
    mov a,@r0
    call fn_push
    jmp pn_next
pn_stop:
    mov r5,#0
pn_skip:
    clr a                       ; still has to emit a byte -- the payload is a
    call fn_push                ; fixed 256, and a skipped byte shortens it
pn_next:
    inc r0
    djnz r2,pn_l
    mov r2,#0                   ; pad the remaining 256-1-NAMELEN bytes
    mov a,#NAMELEN+1
    cpl a
    inc a
    add a,r2
    mov r2,a
pn_pad:
    clr a
    call fn_push
    djnz r2,pn_pad
    ret

; show_host: the highlighted host's name on the single-character row, and a
; four-character status cell ("H nn") on the quad row.
show_host:
    call gfx_off
    mov a,#P1_SEL_XRAM
    outl p1,a
    mov r0,#V_HOST              ; host N's record starts at offset N*32
    mov a,@r0
    rl a
    rl a
    rl a
    rl a
    rl a
    mov r1,a
    mov r0,#V_NAME
    mov r2,#NAMELEN
    mov r4,#1
sh_l:
    mov a,r4
    jz sh_pad
    mov a,r1
    call fn_rx
    jnz sh_put
    mov r4,#0
sh_pad:
    mov a,#' '
sh_put:
    mov @r0,a
    inc r0
    inc r1
    djnz r2,sh_l
    mov r0,#V_HOST
    mov a,@r0
    mov r1,#'H'
    call set_stat
    mov a,#P1_SEL_VDC
    outl p1,a
    call show_quad
    call show_name
    call gfx_on
    ret

; show_file: the current directory entry, and an "F nn" status cell.
show_file:
    call gfx_off
    mov r0,#V_POS
    mov a,@r0
    mov r1,#'F'
    call set_stat
    mov a,#P1_SEL_VDC
    outl p1,a
    call show_quad
    call show_name
    call gfx_on
    ret

; set_stat: build the quad-row status cell -- r1 = the label character,
; A = a number 0-99 rendered as two digits.
set_stat:
    call to_dec                 ; r5 = tens, r6 = units
    mov r0,#V_STAT
    mov a,r1
    mov @r0,a
    inc r0
    mov a,r5
    mov @r0,a
    inc r0
    mov a,r6
    mov @r0,a
    inc r0
    mov a,#' '
    mov @r0,a
    ret

; gfx_on: reveal the foreground. Characters cannot be changed while it is on,
; so every redraw blanks first and re-enables here.
gfx_on:
    mov r0,#vdc_control
    mov a,#vdc_ctrl_fore
    movx @r0,a
    ret
gfx_off:
    mov r0,#vdc_control
    clr a
    movx @r0,a
    ret

; wait_input: block until the stick is centred, then until it moves. Returns
; A = 1 up, 2 down, 3 fire.
wait_input:
wi_rel:
    call joy_read
    jnz wi_rel
wi_wait:
    call joy_read
    jz wi_wait
    mov r4,a
    xrl a,#4
    jz wi_fire
    mov a,r4
    ret
wi_fire:
    mov a,#3
    ret
