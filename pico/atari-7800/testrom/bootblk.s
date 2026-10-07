; bootblk.s -- the 4K the cart serves at $F000-$FFFF at power-on.
;
; The BIOS checks it as it would any cartridge: $FE00-$FE7F must read the same
; twice, $FFF8/$FFF9 must say "7800 cart, hash from $F000", the reset vector
; must point at $F000 or above, and the 120-byte signature at $FF80 must
; match the hash. tools/mkbootblk.py signs it once with 7800sign; the result
; is baked into the firmware, so nothing here may change without re-signing.
;
; The BIOS leaves INPTCTRL unlocked, D and I set; the loader takes it from
; there. If the cart's own magic does not read back at $0C09, the console
; cannot see the mailbox below $1000: a red screen says so.

        .include "fujinet.inc"

        .segment "BOOT"
        .res    $0F00, $FF              ; $F000-$FEFF

        .segment "ENTRY"                ; $FF00
entry:  lda     FN_MAGIC
        cmp     #'F'
        bne     nomail
        lda     FN_MAGIC+1
        cmp     #'N'
        bne     nomail
        jmp     FN_LOADER_BOOT
nomail: cld
        lda     #$34                    ; red, with DMA off: only the background
        sta     BACKGRND
@hang:  jmp     @hang
irq:    rti

        .segment "SIG"                  ; $FF80-$FFF7, written by 7800sign
        .res    $78, $FF

        .segment "FLAGS"                ; $FFF8
        .byte   $FF                     ; region check: high nibble $F, bit 0
        .byte   $F7                     ; 7800 cart; the hash starts at $F000
        .word   irq                     ; NMI
        .word   entry                   ; RESET
        .word   irq                     ; IRQ
