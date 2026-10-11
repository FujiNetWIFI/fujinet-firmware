; hello.s -- M0: the boot block, the loader, MARIA text and a frame counter.
;
; No mailbox traffic; it carries the claim so the cart runs it as an app.
; Also shows what the cart's INPTCTRL model says, which must be unlocked:
; nothing on this path may have locked the console.

        .include "fujinet.inc"

        .import disp_at, disp_str, disp_hex, disp_on, disp_vbl, disp_putc
        .export main, nmi

        .segment "ZEROPAGE"
frames: .res 1

        .segment "CODE"

.macro  PUTS    row, col, str
        lda     #row
        ldx     #col
        jsr     disp_at
        lda     #<str
        sta     FN_PTR
        lda     #>str
        sta     FN_PTR+1
        jsr     disp_str
.endmacro

.proc main
        PUTS    2, 2, title
        PUTS    4, 2, hello
        PUTS    6, 2, m0
        PUTS    8, 2, tinpt
        PUTS    20, 2, frames_lbl
        jsr     disp_on
loop:   jsr     disp_vbl
        inc     frames
        lda     #20
        ldx     #10
        jsr     disp_at
        lda     frames
        jsr     disp_hex
        lda     #8
        ldx     #12
        jsr     disp_at
        lda     FN_INPTCTRL
        jsr     disp_hex
        lda     #' '
        jsr     disp_putc
        lda     FN_INPTLOCK
        jsr     disp_hex
        jmp     loop
.endproc

.proc nmi
        rti
.endproc

        .segment "RODATA"
title:      .byte "FUJINET ATARI 7800", 0
hello:      .byte "HELLO, WORLD", 0
m0:         .byte "M0: BOOT BLOCK + LOADER + 320A TEXT", 0
tinpt:      .byte "INPTCTRL: ", 0
frames_lbl: .byte "FRAMES: ", 0
