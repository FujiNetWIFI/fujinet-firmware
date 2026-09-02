; fujilib.inc -- shared mailbox + display helpers for the O2 FujiNet carts.
;
; Include after the cart's own code. Pins itself to page $500: conditional
; jumps on the 8048 are page-relative, so a routine that drifts across a
; 256-byte boundary silently stops assembling.
;
; Register convention across these helpers:
;   r0  MOVX address / VDC register walker
;   r1  table or string pointer
;   r2  loop counter, and NPARAM into fn_begin
;   r3  X position, and TXLEN_HI into fn_begin
;   r4  scratch
;   r5  sequence number (owned by fn_fire)
;   r6  fn_reg scratch
;   r7  fn_reg register index

    org 500h

; vdc_init: blank the display and clear the power-on garbage. The quad
; registers matter as much as the singles and sprites -- leaving them shows
; up as bars in the top-left.
vdc_init:
    mov a,#P1_SEL_VDC
    outl p1,a
    mov r0,#vdc_control
    clr a
    movx @r0,a
    mov r0,#vdc_spr0_ctrl
    mov r2,#16
    call vfill
    mov r0,#vdc_spr0_shape
    mov r2,#32
    call vfill
    mov r0,#vdc_char0
    mov r2,#48
    call vfill
    mov r0,#vdc_quad0
    mov r2,#64
    call vfill
    mov r0,#vdc_color
    mov a,#col_bck_blue|col_grd_black
    movx @r0,a
    ret
vfill:
    clr a
vfl:
    movx @r0,a
    inc r0
    djnz r2,vfl
    ret

; fn_reg: write A into the mailbox register named by r7. P1 = P1_SEL_XRAM.
fn_reg:
    mov r6,a
    mov r0,#FN_W_REGSEL
    mov a,r7
    movx @r0,a
    mov r0,#FN_W_REGDATA
    mov a,r6
    movx @r0,a
    ret

; fn_push: append A to the TX byte stream. P1 = P1_SEL_XRAM.
fn_push:
    mov r0,#FN_W_DATA
    movx @r0,a
    ret

; fn_par1: append a one-byte parameter (size marker, then the value in A).
fn_par1:
    mov r4,a
    mov a,#1
    call fn_push
    mov a,r4
    call fn_push
    ret

; fn_read: A = offset within the cart's mailbox page -> A = the byte there.
; MOVP reads the page the PC is in, so the load itself lives at $F00.
fn_read:
    sel mb1
    call fn_get
    sel mb0
    ret

; fn_begin: start a request. A = command, r2 = nparam, r3 = TXLEN_HI.
; TXLEN_LO is always 0 -- the only payload any of these commands carries is
; the 256-byte NUL-padded path.
fn_begin:
    mov r4,a
    clr a
    mov r7,#FN_REG_DATA_RST
    call fn_reg
    mov a,#FUJI_DEVICEID
    mov r7,#FN_REG_DEVICE
    call fn_reg
    mov a,r4
    mov r7,#FN_REG_CMD
    call fn_reg
    mov a,r2
    mov r7,#FN_REG_NPARAM
    call fn_reg
    clr a
    mov r7,#FN_REG_TXLEN_LO
    call fn_reg
    mov a,r3
    mov r7,#FN_REG_TXLEN_HI
    call fn_reg
    ret

    org 570h
; fn_fire: launch the transaction and wait for it.
; Returns A = the reply command (FUJICMD_ACK / _NAK), or 0 on timeout.
;
; The sequence number comes from the CART's persisted ACKSEQ, never from a
; local counter: a console RESET restarts this program but not the cartridge,
; so a local counter would recompute a value the cart has already acked and no
; further request would ever be sent -- while the screen kept showing the
; previous reply. Sequence 0 is reserved.
fn_fire:
    mov a,#FN_R_ACKSEQ&0FFh
    call fn_read
    inc a
    jnz ffseq
    inc a
ffseq:
    mov r5,a
    mov r0,#FN_W_SEQ
    movx @r0,a
    mov r2,#0
ffpoll:
    mov a,#FN_R_ACKSEQ&0FFh
    call fn_read
    xrl a,r5
    jz ffok
    djnz r2,ffpoll
    clr a
    ret
ffok:
    mov a,#FN_R_REPLY_CMD&0FFh
    call fn_read
    ret

    org 5A0h
; show_char: draw ASCII in A at VDC register r0, X position r3; advances both.
; P1 = P1_SEL_VDC.
show_char:
    mov r4,a
    mov a,#CMAP_YP
    movx @r0,a
    inc r0
    mov a,r3
    movx @r0,a
    inc r0
    sel mb1
    mov a,r4
    add a,#(cm_lo-20h)&0FFh
    call cm_get
    sel mb0
    movx @r0,a
    inc r0
    sel mb1
    mov a,r4
    add a,#(cm_hi-20h)&0FFh
    call cm_get
    sel mb0
    movx @r0,a
    inc r0
    mov a,r3
    add a,#8
    mov r3,a
    ret

    org 5D0h
; puts_e: print the NUL-terminated string at offset A of page $D00.
puts_e:
    mov r1,a
psl:
    sel mb1
    mov a,r1
    call str_get
    sel mb0
    jz psdone
    call show_char
    inc r1
    jmp psl
psdone:
    ret

    include "charmap.inc"

; Cart text lives in its own page: the generated character map fills $E02-$EC1
; and leaves too little behind it for messages plus a path.
    org 0D00h
str_get:                    ; A = offset in page $D00 -> A = byte there
    movp a,@a
    ret
