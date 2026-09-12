; cfgtail.asm -- CONFIG's fixed half: the cold stub and the bank trampoline.
;
; $1800-$1F1F is the mailbox -- text planes, reply window, control page, TX
; page, status -- and the cartridge paints it. The client owns $1F20-$1FFB,
; which fuji_mailbox.h calls the fixed tail, plus the vectors.
;
; TWO ROUTINES LIVE HERE AND NEITHER COULD LIVE ANYWHERE ELSE.
;
; CFGOTO switches bank. The store that switches is the last instruction
; fetched from the OLD bank; the very next fetch comes from the new one. So the
; jump that follows it has to be at an address that does not move, or the
; program counter lands in whatever the new bank happens to have at that
; offset. This is the whole reason a fixed tail exists.
;
; CFCOLD is where the RESET vector points. It has to be here for the same
; reason plus one more: this console has no reset line to the cartridge, so
; the RESET switch restarts the 6507 with WHATEVER BANK WAS LAST SELECTED
; still mapped. A cold stub in bank 0 would simply not be there.

        CPU     6502
        INCLUDE "vcs.inc"
        INCLUDE "fujinet.inc"
        INCLUDE "cfgdefs.inc"

; CFGOTO is not a label here: cfgdefs.inc gives it a fixed address and this
; ORG is what makes that true, so the two cannot drift apart.
        ORG     CFGOTO

; ---------------------------------------------------------------------------
; CFGOTO -- select bank A and enter it at $1000.
;
; `sta FNRSEL,x` is the documented-safe indexed form: the base low byte is $00
; so the index cannot carry, which means the dummy read that STA abs,X always
; performs lands on the same address as the write. One parked access, and a
; bank op ignores its data anyway.
        clc
        adc     #FH_BANK
        tax
        sta     FNRSEL,x
        jmp     $1000

; ---------------------------------------------------------------------------
; CFCOLD -- power-on and RESET.
;
; The gate is opened here, inline, because CFGOTO needs it: banking is a
; control-page op and the control page is dead until an ordered pair of stores
; carrying two specific values arrives. A 7800's BIOS probing cartridge space
; cannot produce that pair, which is the point of the gate.
CFCOLD: sei
        cld
        ldx     #$FF
        txs
        lda     #0
CFCL1:  sta     $00,x           ; $00-$7F is the TIA, $80-$FF is RAM
        dex
        bne     CFCL1
        sta     $00

        lda     #2
        sta     VBLANK          ; blanked until a bank starts its display

        lda     #FNAM1
        sta     FNRSEL+FH_ARM1
        lda     #FNAM2
        sta     FNRSEL+FH_ARM2

        ; The working directory is CARTRIDGE state and survives a console
        ; reset, so a cold start has to empty it or the browser comes back
        ; somewhere the host screen does not expect.
        lda     #FP_RST
        sta     FNRSEL+FH_PATHO

        lda     #BANKHST
        jmp     CFGOTO

        ORG     $1FFC
        DW      CFCOLD
        DW      CFCOLD

        END
