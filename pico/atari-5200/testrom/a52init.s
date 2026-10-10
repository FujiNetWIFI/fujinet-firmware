; a52init.s -- start-up and interrupts for a FujiNet 5200 client.
;
; The header's $BFFD is $FF, so the BIOS jumps straight here with nothing
; set up. This does what the BIOS would have, without depending on which
; BIOS it is: the chips cleared, the RAM vectors at $0200-$021B, and a VBI
; and keypad handler that keep the BIOS's zero-page layout, so cc65's
; atari5200 library works unchanged on top of them.
;
; The console's NMI and IRQ vectors are the BIOS's, and both revisions
; enter through $FCA2 / $FC00 to JMP (VVBLKI) / JMP (VIMIRQ) with nothing
; pushed: these handlers save what they use.

        .include "fujinet.inc"

        .import main
        .export start

HOLD_FRAMES = 4

        .segment "STARTUP"

.proc start
        sei
        cld
        ldx     #$FF
        txs
        lda     #0
        sta     NMIEN
        sta     DMACTL
        sta     IRQEN
        tax
clr:    sta     POKEY,x         ; every chip register to 0, as the BIOS does
        sta     GTIA,x
        sta     ANTIC,x
        sta     $00,x           ; and the zero page
        sta     $0200,x
        inx
        bne     clr
        ldx     #vecs_end - vecs - 1
vec:    lda     vecs,x
        sta     VIMIRQ,x
        dex
        bpl     vec
        lda     #0              ; no key held
        sta     hold
        lda     #$02            ; keypad scan on
        sta     SKCTL
        lda     #$40            ; its IRQ, alone
        sta     POKMSK
        sta     IRQEN
        sta     NMIEN           ; the VBI, no DLIs
        cli
        jmp     main
.endproc

        .segment "CODE"

vecs:   .word   irq, vbi, vbi_exit, dli, kbd, kbd_done
        .word   pla_rti, pla_rti, pla_rti, pla_rti, pla_rti, pla_rti, pla_rti, pla_rti
vecs_end:

; The BIOS's $FCB8, register for register.
.proc vbi
        pha
        txa
        pha
        tya
        pha
        inc     RTCLOKL
        bne     :+
        inc     ATRACT
        inc     RTCLOKH
:       lda     hold            ; a held key's repeats keep this above zero
        beq     :+
        dec     hold
:       lda     CRITIC
        bne     vbi_exit
        lda     SDLSTH
        sta     DLISTH
        lda     SDLSTL
        sta     DLISTL
        lda     SDMCTL
        sta     DMACTL
        ldy     ATRACT
        bpl     :+
        ldy     #$80
        sty     ATRACT
:       ldx     #8
col:    lda     PCOLR0,x
        cpy     #$80
        bcc     :+
        eor     RTCLOKH
        and     #$F6
:       sta     COLPM0,x
        dex
        bpl     col
        ldx     #7
pot:    lda     POT0,x
        sta     PADDL0,x
        dex
        bpl     pot
        sta     POTGO
        jmp     (VVBLKD)
.endproc

.proc vbi_exit
        pla
        tay
        pla
        tax
        pla
        rti
.endproc

dli:    rti

; Only the keypad's IRQ is ever enabled; anything else is acknowledged.
.proc irq
        pha
        lda     IRQST
        and     #$40
        bne     other
        lda     #$BF
        sta     IRQEN
        lda     POKMSK
        sta     IRQEN
        jmp     (VKYBDI)
other:  lda     #0
        sta     IRQEN
        lda     POKMSK
        sta     IRQEN
        pla
        rti
.endproc

pla_rti:
        pla
        rti

; The BIOS's $FD02: KBCODE through its table, then VKYBDF.
.proc kbd
        txa
        pha
        tya
        pha
        lda     KBCODE
        lsr     a
        and     #$0F
        tax
        lda     keys,x
        jmp     (VKYBDF)
.endproc

; 0-9, $0A '*', $0B '#', $0C START, $0D PAUSE, $0E RESET; KEY = code + 1.
; Without debounce (SKCTL $02, the BIOS's) POKEY interrupts on every scan of
; a held key, and SKSTAT's key-down bit drops between scans: a repeat of the
; key while `hold` runs only restarts it, so each press counts once.
.proc kbd_done
        ldx     #HOLD_FRAMES
        cmp     held
        bne     @new
        ldy     hold
        stx     hold
        cpy     #0
        bne     @same
@new:   stx     hold
        sta     held
        clc
        adc     #1
        sta     KEY
        lda     #0
        sta     ATRACT
@same:  jmp     vbi_exit
.endproc

        .segment "BSS"
held:   .res    1               ; the last key
hold:   .res    1               ; frames until it may count again

        .segment "CODE"

keys:   .byte   $FF, $0B, $00, $0A, $0E, $09, $08, $07
        .byte   $0D, $06, $05, $04, $0C, $03, $02, $01
