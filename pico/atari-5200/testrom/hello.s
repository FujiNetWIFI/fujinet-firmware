; hello.s -- M0: the cart serves a client the BIOS hands straight to, and it
; runs: text from a font in ROM, the VBI ticking, the keypad answering, and
; the cart's status page answering 'F','N'.

        .include "fujinet.inc"

        .import disp_init, disp_at, disp_str, disp_hex, disp_on
        .import fn_chk
        .export main

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
        jsr     disp_init
        PUTS    2, 2, title
        PUTS    4, 2, tcart
        jsr     fn_chk
        bne     nocart
        PUTS    4, 8, tyes
        jmp     on
nocart: PUTS    4, 8, tno
on:     PUTS    6, 2, tframe
        PUTS    8, 2, tkey
        jsr     disp_on
loop:   lda     #6
        ldx     #9
        jsr     disp_at
        lda     RTCLOKH
        jsr     disp_hex
        lda     RTCLOKL
        jsr     disp_hex
        lda     #8
        ldx     #9
        jsr     disp_at
        lda     KEY
        jsr     disp_hex
        jmp     loop
.endproc

        .segment "RODATA"
title:  .byte "FUJINET ATARI 5200 M0", 0
tcart:  .byte "CART", 0
tyes:   .byte "FN OK", 0
tno:    .byte "NO MAILBOX", 0
tframe: .byte "FRAME", 0
tkey:   .byte "KEY", 0

        .include "header.inc"
