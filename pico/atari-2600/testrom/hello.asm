; hello.asm -- the cart-rendered 48-pixel text display, with no mailbox or network.
;
; The planes at $1800-$1AFF are baked by tools/vcsfont.py, the renderer
; vcs_render.c is tested against; a plain 4K cart stock MAME boots.
; emu/dispcheck.py compares the raster with it.

        CPU     6502
        INCLUDE "vcs.inc"

; ---------------- zero page ----------------
PAD3    EQU     $81             ; scratch: a 3-cycle store used only as a delay
SAVSP   EQU     $82             ; stack pointer, parked across the kernel

        ORG     $1000

START:  sei
        cld
        ldx     #$FF
        txs
        lda     #0
CLRLP:  sta     $00,x           ; $00-$7F is the TIA, $80-$FF is RAM
        dex
        bne     CLRLP
        sta     $00

        jsr     DINIT
        jmp     DLOOP

APPVBL: rts                     ; this client needs no per-frame work

        INCLUDE "fujidisp.inc"

; ---------------- the text planes ----------------
        ORG     $1800
        INCLUDE "screen.inc"

; ---------------- the fixed tail ----------------
        ORG     $1FFC
        DW      START           ; RESET
        DW      START           ; BRK -- a runaway reboots rather than hangs

        END
