; hello.s -- M0: the toolchain, the header, the font and a frame counter.
;
; Plain NROM with CHR-RAM and no mailbox traffic at all (it still carries the
; claim, so the cart keeps the mailbox alive; nothing here touches it). Runs
; in stock MAME as well as behind the FujiNet cart.

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

; Once a frame: the counter, written during vblank, then the scroll restored
; -- PPU_ADDR writes disturb it.
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
