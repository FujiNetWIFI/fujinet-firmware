; fujidisp.s -- the bring-up clients' text: ANTIC mode 2, 40 x 24, from a
; font in ROM and a screen in RAM. Characters are ASCII on the way in.

        .include "fujinet.inc"

        .export disp_init, disp_at, disp_putc, disp_str, disp_hex, disp_dec
        .export disp_rpl, disp_on

        .segment "CODE"

; disp_init -- clear the screen, point ANTIC at it; the display is off
; until disp_on.
.proc disp_init
        lda     #<SCREEN
        sta     DISP_PTR
        lda     #>SCREEN
        sta     DISP_PTR+1
        lda     #0
        ldx     #4              ; 4 x 256 covers 960
        ldy     #0
clr:    sta     (DISP_PTR),y
        iny
        bne     clr
        inc     DISP_PTR+1
        dex
        bne     clr
        lda     #<dlist
        sta     SDLSTL
        lda     #>dlist
        sta     SDLSTH
        lda     #>font
        sta     CHBASE
        lda     #2
        sta     CHACTL
        lda     #$0E            ; text luminance
        sta     COLOR1
        lda     #$84            ; blue background
        sta     COLOR2
        lda     #$00
        sta     COLOR4
        lda     #0
        tax
        jmp     disp_at
.endproc

.proc disp_on
        lda     #$22            ; normal playfield, display list DMA
        sta     SDMCTL
        rts
.endproc

; disp_at -- cursor to row A, column X.
.proc disp_at
        stx     DISP_TMP
        tax
        lda     #<SCREEN
        sta     DISP_PTR
        lda     #>SCREEN
        sta     DISP_PTR+1
        cpx     #0
        beq     col
row:    clc
        lda     DISP_PTR
        adc     #40
        sta     DISP_PTR
        bcc     :+
        inc     DISP_PTR+1
:       dex
        bne     row
col:    clc
        lda     DISP_PTR
        adc     DISP_TMP
        sta     DISP_PTR
        bcc     :+
        inc     DISP_PTR+1
:       rts
.endproc

; disp_putc -- ASCII A at the cursor, which advances. Y is kept.
.proc disp_putc
        cmp     #$20
        bcc     ctl
        cmp     #$60
        bcs     put
        sbc     #$1F            ; $20-$5F -> $00-$3F (carry was clear: -$20)
        bcs     put
ctl:    adc     #$40            ; $00-$1F -> $40-$5F
put:    sty     DISP_TMP
        ldy     #0
        sta     (DISP_PTR),y
        ldy     DISP_TMP
        inc     DISP_PTR
        bne     :+
        inc     DISP_PTR+1
:       rts
.endproc

; disp_str -- the NUL-terminated string at FN_PTR.
.proc disp_str
        ldy     #0
loop:   lda     (FN_PTR),y
        beq     done
        jsr     disp_putc
        iny
        bne     loop
done:   rts
.endproc

.proc disp_hex
        pha
        lsr     a
        lsr     a
        lsr     a
        lsr     a
        jsr     nib
        pla
        and     #$0F
nib:    cmp     #10
        bcc     :+
        adc     #6
:       adc     #'0'
        jmp     disp_putc
.endproc

; disp_dec -- A as up to three decimal digits.
.proc disp_dec
        ldx     #0
        ldy     #100
        jsr     digit
        ldy     #10
        jsr     digit
        ora     #'0'
        jmp     disp_putc
digit:  sty     DISP_TMP+1
        ldy     #0
:       cmp     DISP_TMP+1
        bcc     :+
        sbc     DISP_TMP+1
        iny
        bne     :-
:       pha
        tya
        bne     show
        cpx     #0
        beq     skip
show:   ora     #'0'
        jsr     disp_putc
        ldx     #1
skip:   pla
        rts
.endproc

; disp_rpl -- up to Y characters of the reply from offset X, to a NUL.
.proc disp_rpl
        sty     DISP_TMP+1
loop:   lda     FN_REPLY,x
        beq     done
        jsr     disp_putc
        inx
        dec     DISP_TMP+1
        bne     loop
done:   rts
.endproc

        .segment "DLIST"

dlist:  .byte   $70, $70, $70
        .byte   $42, <SCREEN, >SCREEN
        .repeat 23
        .byte   $02
        .endrepeat
        .byte   $41, <dlist, >dlist

        .segment "FONT"

font:
        .include "font.inc"
