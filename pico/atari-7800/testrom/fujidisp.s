; fujidisp.s -- text for the bring-up clients: MARIA 320A, 40 x 24.
;
; One zone per text row; each zone's display list is two indirect headers of
; 20 characters, so MARIA reads character codes from the screen buffer and
; their pixels from the font at CHARBASE. Everything MARIA reads but the font
; lives in the cart's RAM. Character = ASCII code; +$80 is reverse video.

        .include "fujinet.inc"

        .export disp_init, disp_on, disp_off, disp_at, disp_str, disp_hex
        .export disp_rpl, disp_putc, disp_dec, disp_vbl

COLS    = 40
ROWS    = 24
SCR     = $4000                 ; ROWS x COLS character codes
DLS     = $4400                 ; ROWS display lists, 12 bytes each
DLL     = $4600                 ; the display list list
NULLDL  = $46F0                 ; an empty display list

        .segment "CODE"

; disp_init -- cleared screen, display lists, palette. DMA stays off.
.proc disp_init
        lda     #' '
        ldx     #0
@clr:   sta     SCR,x
        sta     SCR+$100,x
        sta     SCR+$200,x
        sta     SCR+$300,x
        inx
        bne     @clr
        lda     #0
        sta     NULLDL
        sta     NULLDL+1

        ; the display lists: [lo, $60, hi, pal 0 / 20 wide, hpos] twice, then
        ; 0, 0; ROWS x 12 bytes is more than Y reaches, so through a pointer
        ldx     #0              ; row
@dl:    lda     dllo,x
        sta     DISP_PTR
        lda     dlhi,x
        sta     DISP_PTR+1
        ldy     #0
        lda     rowlo,x
        sta     (DISP_PTR),y    ; +0
        ldy     #5
        clc
        adc     #20
        sta     (DISP_PTR),y    ; +5
        lda     rowhi,x
        adc     #0
        ldy     #7
        sta     (DISP_PTR),y    ; +7
        lda     rowhi,x
        ldy     #2
        sta     (DISP_PTR),y    ; +2
        lda     #$60            ; 320A, extended header, indirect
        ldy     #1
        sta     (DISP_PTR),y
        ldy     #6
        sta     (DISP_PTR),y
        lda     #$0C            ; palette 0, 20 bytes
        ldy     #3
        sta     (DISP_PTR),y
        ldy     #8
        sta     (DISP_PTR),y
        lda     #80
        ldy     #9
        sta     (DISP_PTR),y
        lda     #0
        ldy     #4
        sta     (DISP_PTR),y
        ldy     #10
        sta     (DISP_PTR),y
        iny
        sta     (DISP_PTR),y
        inx
        cpx     #ROWS
        bne     @dl

        ; the DLL: 25 blank lines, the rows, 64 blank lines
        ldx     #0
        ldy     #0
@top:   lda     top,x
        sta     DLL,y
        iny
        inx
        cpx     #6
        bne     @top
        ldx     #0
@rows:  lda     #7              ; 8 lines
        sta     DLL,y
        lda     dlhi,x
        sta     DLL+1,y
        lda     dllo,x
        sta     DLL+2,y
        iny
        iny
        iny
        inx
        cpx     #ROWS
        bne     @rows
        ldx     #4
@bot:   lda     #15
        sta     DLL,y
        lda     #>NULLDL
        sta     DLL+1,y
        lda     #<NULLDL
        sta     DLL+2,y
        iny
        iny
        iny
        dex
        bne     @bot

        lda     #>font
        sta     CHARBASE
        lda     #0
        sta     BACKGRND
        lda     #$0F
        sta     P0C1
        sta     P0C2
        sta     P0C3
        lda     #>DLL
        sta     DPPH
        lda     #<DLL
        sta     DPPL
        rts
.endproc

; disp_vbl -- wait for the start of the next vertical blank.
.proc disp_vbl
@in:    bit     MSTAT
        bmi     @in
@out:   bit     MSTAT
        bpl     @out
        rts
.endproc

; disp_on -- DMA on at a frame edge: 320A, one-byte characters.
.proc disp_on
        jsr     disp_vbl
        lda     #$43
        sta     CTRL
        rts
.endproc

; disp_off -- DMA off.
.proc disp_off
        lda     #$7F
        sta     CTRL
        rts
.endproc

; disp_at -- cursor to row A (0-23), column X (0-39).
.proc disp_at
        tay
        txa
        clc
        adc     rowlo,y
        sta     DISP_PTR
        lda     rowhi,y
        adc     #0
        sta     DISP_PTR+1
        rts
.endproc

; disp_putc -- one character at the cursor.
.proc disp_putc
        ldy     #0
        sta     (DISP_PTR),y
        inc     DISP_PTR
        bne     @ok
        inc     DISP_PTR+1
@ok:    rts
.endproc

; disp_str -- the NUL-terminated string at FN_PTR, at most 40 characters;
; the cursor ends after it.
.proc disp_str
        ldy     #0
@l:     lda     (FN_PTR),y
        beq     advance
        sta     (DISP_PTR),y
        iny
        cpy     #COLS
        bne     @l
        ; fall through
.endproc

; advance -- the cursor moves on by Y.
.proc advance
        tya
        clc
        adc     DISP_PTR
        sta     DISP_PTR
        bcc     @d
        inc     DISP_PTR+1
@d:     rts
.endproc

; disp_rpl -- up to Y bytes of the reply window from offset X, stopping at a
; NUL; the cursor ends after them.
.proc disp_rpl
        sty     DISP_TMP
        ldy     #0
@l:     lda     FN_REPLY,x
        beq     @d
        sta     (DISP_PTR),y
        inx
        iny
        cpy     DISP_TMP
        bne     @l
@d:     jmp     advance
.endproc

; disp_hex -- A as two hex digits.
.proc disp_hex
        pha
        lsr
        lsr
        lsr
        lsr
        tax
        lda     hex,x
        jsr     disp_putc
        pla
        and     #$0F
        tax
        lda     hex,x
        jmp     disp_putc
.endproc

; disp_dec -- A as up to three decimal digits, no leading zeros.
.proc disp_dec
        ldx     #0
@h:     cmp     #100
        bcc     @hd
        sbc     #100
        inx
        bne     @h
@hd:    sta     DISP_TMP
        txa
        beq     @t0
        ora     #'0'
        jsr     disp_putc
        ldx     #1              ; tens must show from here on
        bne     @t1
@t0:    ldx     #0
@t1:    stx     DISP_TMP+1
        lda     DISP_TMP
        ldx     #0
@t:     cmp     #10
        bcc     @td
        sbc     #10
        inx
        bne     @t
@td:    sta     DISP_TMP
        txa
        bne     @tp
        lda     DISP_TMP+1
        beq     @o
        txa
@tp:    ora     #'0'
        jsr     disp_putc
@o:     lda     DISP_TMP
        ora     #'0'
        jmp     disp_putc
.endproc

        .segment "RODATA"
hex:    .byte   "0123456789ABCDEF"
top:    .byte   15, >NULLDL, <NULLDL, 8, >NULLDL, <NULLDL
rowlo:
        .repeat ROWS, r
        .byte   <(SCR + r * COLS)
        .endrepeat
rowhi:
        .repeat ROWS, r
        .byte   >(SCR + r * COLS)
        .endrepeat
dllo:
        .repeat ROWS, r
        .byte   <(DLS + r * 12)
        .endrepeat
dlhi:
        .repeat ROWS, r
        .byte   >(DLS + r * 12)
        .endrepeat

        .segment "FONT"
font:
        .include "font.inc"
