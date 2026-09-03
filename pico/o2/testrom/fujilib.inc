; fujilib.inc -- shared mailbox + display helpers for the O2 FujiNet carts.
;
; Include after the cart's own code.
;
; ALL CODE MUST STAY BELOW $800. CALL and JMP carry only 11 address bits; the
; twelfth comes from the MB flag, and the assembler emits SEL MB0/MB1 on the
; assumption that MB tracks the PC's own A11. That assumption breaks the moment
; a routine above $7FF calls one below it: the callee's SEL MB0 persists after
; it returns, and the next call from the high routine silently lands 2K lower --
; in the console BIOS. Keeping every callable routine in the low bank removes
; the hazard entirely. Only data lives high ($D00 strings, $E00 character map,
; $F00 mailbox read stub), reached through explicit sel mb1/sel mb0 wrappers.
;
; The include starts at a FIXED address ($4D0) in every cart. Conditional jumps
; are page-relative, so whether a given routine straddles a 256-byte boundary
; depends on where the include happens to land -- letting that vary with the
; size of each cart's main body means a layout fix for one cart breaks another.
; Each cart therefore pads its main body out to $4D0 before including this.
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


    org 4D0h

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
; Runs in register bank 1 so it cannot disturb a caller that is walking a
; buffer with r0 -- which is exactly what every payload loop does.
fn_push:
    sel rb1
    mov r0,#FN_W_DATA
    movx @r0,a
    sel rb0
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

; fn_rx: A = byte offset 0-255 within the reply -> A = that byte.
; Switches the published slice when the offset crosses 128 and remembers which
; slice is live, so a sequential walk costs one mailbox write per 128 bytes.
; P1 = P1_SEL_XRAM on entry (it may have to write FN_REG_RXSLICE).
;
; The reply window is 128 bytes at $F80 precisely so this is shift-and-mask
; work: an 8048 doing a divide by 208 to find a slice would be silly.
; Bank 1 throughout, for the same reason as fn_push: callers walk a destination
; buffer with r0 and a source offset with r1 across this call.
fn_rx:
    sel rb1
    mov r4,a
    anl a,#80h
    jz fnrx_s0
    mov a,#1
fnrx_s0:
    mov r1,a
    mov r0,#V_SLICE
    xrl a,@r0
    jz fnrx_go
    mov a,r1
    mov @r0,a
    mov r7,#FN_REG_RXSLICE
    call fn_reg                 ; also bank 1, so it clobbers nothing of ours
fnrx_go:
    mov a,r4
    anl a,#7Fh
    orl a,#FN_R_DATA&0FFh
    call fn_read
    sel rb0
    ret

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

; show_quad: draw the 4 bytes at V_STAT as one quad-character group on row QY.
;
; One group, not four: quad characters are spaced at DOUBLE pitch, 16 pixels
; apart rather than 8 (o2em's draw_quad advances `off += 16` on top of the 16
; buffer units the glyph itself consumes). Four groups of four would want 256
; pixels on a 160-pixel screen. P1 = P1_SEL_VDC.
show_quad:
    mov r0,#vdc_quad0
    mov r1,#V_STAT
    mov r3,#QX
    mov r2,#4
    mov a,#QY
    movx @r0,a                  ; +0 Y (the VDC mirrors it to all four records)
    inc r0
    mov a,r3
    movx @r0,a                  ; +1 X
    inc r0
sq_ch:
    mov a,@r1
    mov r4,a
    inc r1
    sel mb1
    mov a,r4
    add a,#(cm_lo-20h)&0FFh
    call cm_get
    sel mb0
    add a,#QDELTA
    movx @r0,a
    inc r0
    mov r6,#0
    jnc sq_hi
    mov r6,#1
sq_hi:
    sel mb1
    mov a,r4
    add a,#(cm_hi-20h)&0FFh
    call cm_get
    sel mb0
    xrl a,r6
    movx @r0,a
    inc r0
    inc r0                      ; step over the next record's own Y and X,
    inc r0                      ; which the VDC ignores for all but the first
    djnz r2,sq_ch
    ret

; to_dec: A (0-99) -> r5 = tens ASCII, r6 = units ASCII.
; r5/r6, never r4: show_char uses r4 to hold the character it is drawing.
to_dec:
    mov r5,#'0'
td_l:
    add a,#0F6h                 ; -10
    jnc td_u
    mov r6,a
    mov a,r5
    inc a
    mov r5,a
    mov a,r6
    jmp td_l
td_u:
    add a,#10
    add a,#'0'
    mov r6,a
    ret

; show_name: draw the NAMELEN bytes at V_NAME across the single characters.
; P1 = P1_SEL_VDC.
show_name:
    mov r0,#vdc_char0
    mov r3,#NX
    mov r2,#NAMELEN
sn_l:
    mov r1,#V_NAME              ; recomputed each pass rather than held across
    mov a,r2                    ; show_char, which owns r4 and advances r0/r3
    cpl a
    inc a
    add a,#NAMELEN
    add a,r1
    mov r1,a
    mov a,@r1
    call show_char
    djnz r2,sn_l
    ret

    org 600h                    ; its jz must not straddle $5FF
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

; joy_read: read stick 0 through the BIOS and return a direction code in A:
;   0 = centred, 1 = up, 2 = down, 3 = fire, 4 = left, 5 = right
; The keyboard and joysticks can only be scanned from internal ROM -- P20-P23
; double as A8-A11 and toggle on every external fetch -- so this has to go
; through the BIOS entry point rather than poking the port directly.
joy_read:
    mov a,#P1_SEL_JOY
    outl p1,a
    mov r1,#0
    call getjoystick
    mov a,#P1_SEL_VDC           ; put the port back before anything else looks
    outl p1,a
    jf0 jr_fire
    mov a,r3
    jz jr_horz
    xrl a,#0FFh
    jz jr_up
    mov a,#2
    ret
jr_up:
    mov a,#1
    ret
jr_horz:
    mov a,r2
    jz jr_none
    xrl a,#0FFh
    jz jr_left
    mov a,#5
    ret
jr_left:
    mov a,#4
    ret
jr_fire:
    mov a,#3
    ret
jr_none:
    clr a
    ret

    include "charmap.inc"

; Cart text lives in its own page: the generated character map fills $E02-$EC1
; and leaves too little behind it for messages plus a path.
    org 0D00h
str_get:                    ; A = offset in page $D00 -> A = byte there
    movp a,@a
    ret
