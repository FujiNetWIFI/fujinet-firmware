; hello.asm -- M0: the cart-rendered 48-pixel text display, proven standalone.
;
; This is the rung worth failing early. Every sibling console in this family
; had a character generator or a framebuffer and rendered console-side; the
; 2600 has neither, so the cartridge composes GRP bytes and the console runs a
; cycle-exact kernel over them. Nothing else in the port has a template for
; that, and if it does not come up cleanly the CONFIG rung is dead and the
; memory map changes shape.
;
; So this image contains NO mailbox and NO network: the text planes are baked
; at $1800-$1AFF by tools/vcsfont.py, which is the same renderer the cartridge
; runs in C (firmware/src/vcs_render.c, byte-compared against it by
; host_test/test_render.c). It is a plain 4K cartridge and stock MAME boots it
; with no patching whatsoever -- which is the point. The display is proven
; before a single line of emulator work exists.
;
; Exit test: emu/dispcheck.py decodes the raster back into plane bytes and
; requires all 756 to be byte-identical to the renderer's.

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
