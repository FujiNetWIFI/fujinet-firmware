; fujidisp.s -- text on the NES for the bring-up clients: an 8x8 font uploaded
; into CHR-RAM, and writes straight into the nametable while rendering is off.
;
; Tile number = ASCII code, so a string is written by storing its bytes to
; PPU_DATA after one PPU_ADDR set. All of this runs with the screen off; a
; client turns it on with disp_on once the text is in place. (CONFIG proper
; will buffer updates for vblank; the bring-up clients do not need to.)

        .include "fujinet.inc"

        .export disp_init, disp_on, disp_off, disp_at, disp_str, disp_hex
        .export disp_rpl, disp_putc, disp_dec

        .segment "CODE"

; disp_init -- font into CHR $0000-$07FF, palette, cleared nametable.
; Rendering must already be off and the PPU warmed up.
.proc disp_init
        ; palette: black background, white text (colour 1)
        lda     #$3F
        sta     PPU_ADDR
        lda     #$00
        sta     PPU_ADDR
        ldx     #0
pal:    lda     palette,x
        sta     PPU_DATA
        inx
        cpx     #32
        bne     pal

        ; tiles $00-$1F: blank
        lda     #$00
        sta     PPU_ADDR
        sta     PPU_ADDR
        ldx     #0
        txa
blank:  sta     PPU_DATA
        sta     PPU_DATA
        inx
        bne     blank           ; 256 x 2 = 512 bytes = 32 tiles

        ; tiles $20-$7F: the font, plane 0 from the table, plane 1 zero
        lda     #<font
        sta     FN_PTR
        lda     #>font
        sta     FN_PTR+1
        ldx     #96
tile:   ldy     #0
row:    lda     (FN_PTR),y
        sta     PPU_DATA
        iny
        cpy     #8
        bne     row
        lda     #0
        ldy     #8
zero:   sta     PPU_DATA
        dey
        bne     zero
        lda     FN_PTR
        clc
        adc     #8
        sta     FN_PTR
        bcc     next
        inc     FN_PTR+1
next:   dex
        bne     tile

        ; nametable 0 and its attributes: spaces, palette 0
        lda     #$20
        sta     PPU_ADDR
        lda     #$00
        sta     PPU_ADDR
        ldx     #4
        ldy     #0
        lda     #' '
clr:    sta     PPU_DATA
        iny
        bne     clr
        dex
        bne     clr             ; 1024 bytes: 960 tiles + 64 attributes (' ' = %00100000, palette 0 everywhere)
        rts
.endproc

; disp_on -- background on, scroll home.
.proc disp_on
        bit     PPU_STATUS
vbl:    bit     PPU_STATUS
        bpl     vbl
        lda     #0
        sta     PPU_SCROLL
        sta     PPU_SCROLL
        lda     #%10000000      ; NMI on, nametable 0, pattern table 0
        sta     PPU_CTRL
        lda     #%00001010      ; background + left 8 pixels
        sta     PPU_MASK
        rts
.endproc

; disp_off -- everything off, for a batch of writes.
.proc disp_off
        lda     #0
        sta     PPU_CTRL
        sta     PPU_MASK
        rts
.endproc

; disp_at -- cursor to row A (0-29), column X (0-31).
.proc disp_at
        pha
        lsr     a
        lsr     a
        lsr     a               ; row / 8 = the high byte's low bits
        clc
        adc     #$20
        sta     PPU_ADDR
        pla
        asl     a
        asl     a
        asl     a
        asl     a
        asl     a               ; row * 32, low byte
        sta     FN_PCNT         ; scratch: the path counter is idle here
        txa
        clc
        adc     FN_PCNT
        sta     PPU_ADDR
        rts
.endproc

; disp_putc -- one character at the cursor.
.proc disp_putc
        sta     PPU_DATA
        rts
.endproc

; disp_str -- the NUL-terminated string at FN_PTR, at most 32 characters.
.proc disp_str
        ldy     #0
loop:   lda     (FN_PTR),y
        beq     done
        sta     PPU_DATA
        iny
        cpy     #32
        bne     loop
done:   rts
.endproc

; disp_rpl -- up to Y bytes of the reply window from offset X, stopping at
; NUL. Zero-copy: the bytes go from the cart's window to the PPU without
; touching console RAM.
.proc disp_rpl
loop:   lda     FN_RPLY,x
        beq     done
        sta     PPU_DATA
        inx
        dey
        bne     loop
done:   rts
.endproc

; disp_hex -- A as two hex digits.
.proc disp_hex
        pha
        lsr     a
        lsr     a
        lsr     a
        lsr     a
        jsr     digit
        pla
        and     #$0F
digit:  cmp     #10
        bcc     num
        adc     #6              ; carry is set: +7 in total
num:    adc     #'0'
        sta     PPU_DATA
        rts
.endproc

; disp_dec -- A as up to three decimal digits, no leading zeros.
.proc disp_dec
        ldx     #0              ; hundreds
h:      cmp     #100
        bcc     hd
        sbc     #100
        inx
        bne     h
hd:     pha
        txa
        beq     tens
        ora     #'0'
        sta     PPU_DATA
tens:   pla
        ldx     #0
t:      cmp     #10
        bcc     td
        sbc     #10
        inx
        bne     t
td:     pha
        txa
        beq     ones
        ora     #'0'
        sta     PPU_DATA
ones:   pla
        ora     #'0'
        sta     PPU_DATA
        rts
.endproc

        .segment "RODATA"
palette:
        .byte   $0F,$30,$30,$30, $0F,$30,$30,$30, $0F,$30,$30,$30, $0F,$30,$30,$30
        .byte   $0F,$30,$30,$30, $0F,$30,$30,$30, $0F,$30,$30,$30, $0F,$30,$30,$30
font:
        .include "font.inc"
