; pokeytest.s -- M2p: a fixed script of POKEY sounds through the cart's
; POKEY at $0450, for tools/pokeycmp.py to compare MAME's recording with the
; firmware's synthesiser. The TIA is never touched, so the recording is the
; POKEY alone. Each step is one row of `steps` (AUDCTL, AUDF1, AUDC1, AUDF2,
; AUDC2), held for 30 frames; tools/pokeycmp.py reads the table back out of
; the built image.

        .include "fujinet.inc"

        .import disp_at, disp_str, disp_hex, disp_on, disp_vbl
        .export main, nmi, steps

POKEY   = $0450
STEPLEN = 6

        .segment "ZEROPAGE"
step:   .res 1
frames: .res 1

        .segment "CODE"

.proc main
        lda     #2
        ldx     #2
        jsr     disp_at
        lda     #<title
        sta     FN_PTR
        lda     #>title
        sta     FN_PTR+1
        jsr     disp_str
        jsr     disp_on
        lda     #0
        sta     POKEY+$0F       ; SKCTL: reset, then run
        lda     #3
        sta     POKEY+$0F
        lda     #0
        sta     step
next:   lda     step
        cmp     #NSTEPS
        beq     done
        asl                     ; x6: the step's row
        sta     frames
        asl
        clc
        adc     frames
        tax
        lda     steps,x
        sta     POKEY+$08       ; AUDCTL
        lda     steps+1,x
        sta     POKEY+$00       ; AUDF1
        lda     steps+2,x
        sta     POKEY+$01       ; AUDC1
        lda     steps+3,x
        sta     POKEY+$02       ; AUDF2
        lda     steps+4,x
        sta     POKEY+$03       ; AUDC2
        lda     #4
        ldx     #2
        jsr     disp_at
        lda     step
        jsr     disp_hex
        lda     #30
        sta     frames
hold:   jsr     disp_vbl
        dec     frames
        bne     hold
        inc     step
        jmp     next
done:   lda     #0
        sta     POKEY+$01
        sta     POKEY+$03
idle:   jmp     idle
.endproc

.proc nmi
        rti
.endproc

        .segment "RODATA"
title:  .byte "FUJINET ATARI 7800 M2P: POKEY", 0
; AUDCTL, AUDF1, AUDC1, AUDF2, AUDC2, pad
steps:
        .byte   $00, $40, $A8, $00, $00, 0     ; pure tone, 64 kHz clock
        .byte   $00, $10, $A8, $00, $00, 0
        .byte   $00, $80, $AF, $00, $00, 0     ; louder, lower
        .byte   $01, $10, $A8, $00, $00, 0     ; 15 kHz clock
        .byte   $40, $C0, $A8, $00, $00, 0     ; channel 1 at 1.79 MHz
        .byte   $00, $20, $88, $00, $00, 0     ; poly17 noise
        .byte   $80, $20, $88, $00, $00, 0     ; poly9 noise
        .byte   $00, $20, $C8, $00, $00, 0     ; poly4
        .byte   $00, $20, $28, $00, $00, 0     ; poly5-gated tone
        .byte   $50, $00, $00, $04, $A8, 0     ; 1+2 joined, 1.79 MHz
        .byte   $00, $00, $18, $00, $00, 0     ; volume only
        .byte   $00, $40, $A6, $60, $A6, 0     ; two tones
        .byte   $04, $40, $A8, $41, $00, 0     ; high-pass 1 by 3 (3 silent)
NSTEPS = (* - steps) / STEPLEN
