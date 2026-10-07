; a78init.s -- reset, the claim and the vectors for the bring-up clients.
;
; Nothing here writes $00-$1F: INPTCTRL is left as the BIOS left it,
; unlocked, so a game booted later can be handed to the console's own BIOS.

        .include "fujinet.inc"

        .import main, nmi, disp_init

        .segment "STARTUP"
reset:  sei
        cld
        lda     #$7F            ; DMA off until the display is built
        sta     CTRL
        ldx     #$FF
        txs
        lda     #0
        ldx     #$40            ; zero page $40-$FF: never the TIA below it
@zp:    sta     $00,x
        inx
        bne     @zp
        sta     DISP_PTR        ; the cart's RAM, $4000-$7FFF
        tay
        ldx     #$40
@pg:    stx     DISP_PTR+1
@b:     sta     (DISP_PTR),y
        iny
        bne     @b
        inx
        cpx     #$80
        bne     @pg
        jsr     disp_init
        jmp     main
irq:    rti

        .segment "CLAIM"
.ifdef BANKED
        .byte   "FUJI", 1, 5, 1, 0, 0, 0    ; version 1, SuperGame + RAM, cart RAM
.else
        .byte   "FUJI", 1, 0, 1, 0, 0, 0    ; version 1, kind auto, cart RAM
.endif

        .segment "VECTORS"
        .byte   $FF, $87                    ; a 7800 cart, hashed from $8000
        .word   nmi, reset, irq
