; bank0.s -- bank 0's own code in fujibank: a transaction through the
; common fujilib, its result on screen, and a byte only this bank has.
; bank1syms.inc (build.sh) gives the common routines' addresses.

        .include "fujinet.inc"
        .include "bank1syms.inc"

        .segment "BANKCODE"

        jmp     entry           ; $A000: far0 calls here
marker: .byte "0"               ; $A003: bank 1 has '1' here

; Returns A = FNE*.
entry:  lda     #6
        ldx     #2
        jsr     disp_at
        lda     #<tb0
        sta     FN_PTR
        lda     #>tb0
        sta     FN_PTR+1
        jsr     disp_str
        lda     marker
        jsr     disp_putc
        jsr     adapter         ; a transaction from banked code
        bne     done
        lda     #6
        ldx     #12
        jsr     disp_at
        ldx     #0              ; the SSID
        ldy     #20
        jsr     disp_rpl
        lda     #FNEOK
done:   rts

tb0:    .byte "BANK ", 0
