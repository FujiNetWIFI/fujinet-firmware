; hello.s -- NROM, CHR-RAM, the font and a frame counter; no mailbox traffic,
; though it carries the claim. Runs in stock MAME too.

        .include "fujinet.inc"

        .import disp_at, disp_str, disp_hex, disp_on, disp_off, disp_dec
        .importzp fn_ptr
        .export main, nmi

        .segment "ZEROPAGE"
frames: .res 1
tick:   .res 1

        .segment "CODE"

.macro  PUTS    row, col, str
        lda     #row
        ldx     #col
        jsr     disp_at
        lda     #<str
        sta     fn_ptr
        lda     #>str
        sta     fn_ptr+1
        jsr     disp_str
.endmacro

.proc main
        PUTS    2, 2, title
        PUTS    4, 2, hello
        PUTS    6, 2, m0
        PUTS    26, 2, frames_lbl
        jsr     disp_on
loop:   lda     tick
wait:   cmp     tick
        beq     wait
        jmp     loop
.endproc

; PPU_ADDR writes disturb the scroll, so restore it after the counter.
.proc nmi
        pha
        inc     frames
        inc     tick
        lda     #26
        ldx     #10
        jsr     disp_at
        lda     frames
        jsr     disp_hex
        lda     #0
        sta     PPU_SCROLL
        sta     PPU_SCROLL
        lda     #%10000000
        sta     PPU_CTRL
        pla
        rti
.endproc

        .segment "RODATA"
title:      .byte "FUJINET NES", 0
hello:      .byte "HELLO, WORLD", 0
m0:         .byte "M0: NROM + CHR-RAM + FONT", 0
frames_lbl: .byte "FRAMES: ", 0
