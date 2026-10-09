; nesinit.s -- the iNES header, the claim, the vectors and a cold start shared
; by every bring-up client. The client supplies `main` and `nmi`.

        .include "fujinet.inc"

        .import main, nmi
        .export start

        .segment "HEADER"
        .byte   "NES", $1A
        .byte   2               ; 2 x 16K PRG
        .byte   0               ; CHR-RAM
        .byte   %00000000       ; horizontal mirroring, mapper 0 low nibble
        .byte   %00000000       ; mapper 0 high nibble, iNES 1.0
        .byte   1               ; 8K PRG-RAM
        .res    7, 0

        .segment "CLAIM"
        .byte   "FUJI"
        .res    6, $FF

        .segment "VECTORS"
        .addr   nmi
        .addr   start
        .addr   irq

        .segment "STARTUP"
.proc start
        sei
        cld
        ldx     #$FF
        txs
        inx
        stx     PPU_CTRL        ; NMI off
        stx     PPU_MASK        ; rendering off
        stx     APU_DMC
        lda     #$40
        sta     APU_FRAME       ; APU frame IRQ off
        stx     APU_STATUS

        bit     PPU_STATUS      ; two vblanks: the PPU is not ready before that
vbl1:   bit     PPU_STATUS
        bpl     vbl1
vbl2:   bit     PPU_STATUS
        bpl     vbl2

        ; clear $0000-$07FF, the console's RAM; ZP too (X is 0)
        txa
clr:    sta     $0000,x
        sta     $0100,x
        sta     $0200,x
        sta     $0300,x
        sta     $0400,x
        sta     $0500,x
        sta     $0600,x
        sta     $0700,x
        inx
        bne     clr

        jsr     disp_init
        jmp     main
.endproc

irq:    rti

        .import disp_init
