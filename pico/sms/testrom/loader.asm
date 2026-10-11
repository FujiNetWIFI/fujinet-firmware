; loader.asm -- the 2K loader page the cart serves at $B800-$BFFF.
;
; It runs from the arena, which the cart serves in every mode, so it can load
; an image over whatever called it. Neither entry returns.
;
;   $B800  boot   load the armed image into the SRAM and start it
;   $B803  config hand the console back to CONFIG
;
; Each window: the cart maps SRAM bank LOAD_K behind $8000-$9FFF and drives
; the image's bytes there; reading all 8K makes the SRAM latch every one.
; Mirrors firmware/include/fuji_mailbox.h by hand.

        org     $B800

ARENA           equ $B000
LOAD_STATE      equ ARENA + $40D
LOAD_SEQ        equ ARENA + $410
HO_C000         equ ARENA + $420
HO_3E           equ ARENA + $421
HO_3F           equ ARENA + $422
HO_VDP          equ ARENA + $423
HO_MIRROR       equ ARENA + $42E
HO_FILL         equ ARENA + $432
REGSEL          equ ARENA + $500
REG_SLICE_ACK   equ REGSEL + $14
HOT_CONFIG      equ REGSEL + $FC
HOT_GO          equ REGSEL + $FD
HOT_SWAP        equ REGSEL + $FE

LOAD_IDLE       equ 0
LOAD_WINDOW     equ 1
LOAD_DONE       equ 2
LOAD_FAILED     equ $80

VDP_CTRL        equ $BF
PSG             equ $7F

        jp      boot
        jp      config

boot:
        di
        ld      sp,$DFF0
        ld      (HOT_SWAP),a            ; the cart goes RESIDENT and starts the load
wait_start:
        ld      a,(LOAD_STATE)
        or      a
        jr      z,wait_start
        cp      LOAD_FAILED
        jp      z,config
        ld      c,0                     ; the window last read
next:
        ld      a,(LOAD_STATE)
        cp      LOAD_DONE
        jp      z,handover
        cp      LOAD_FAILED
        jp      z,config
        ld      a,(LOAD_SEQ)
        cp      c
        jr      z,next
        ld      c,a
        ld      hl,$8000
        ld      b,32                    ; 32 x 256 = the 8K window
page:
        rept    256
        ld      a,(hl)                  ; the read is the copy
        inc     l
        endr
        inc     h
        dec     b
        jp      nz,page
        ld      a,c
        ld      (REG_SLICE_ACK),a
        jp      next

config:
        di
        ld      sp,$DFF0
        ld      (HOT_CONFIG),a
wait_cfg:
        ld      a,(LOAD_STATE)
        cp      LOAD_DONE
        jr      nz,wait_cfg

; Put the console in the state the cart published (what the BIOS left, or a
; console without one), then start the image. No calls from here on: the
; stack is in the RAM being cleared.
handover:
        ld      a,(HO_FILL)
        ld      hl,$C000
        ld      de,$C001
        ld      bc,$1FFF
        ld      (hl),a
        ldir
        ld      a,(HO_C000)
        ld      ($C000),a
        ld      hl,HO_MIRROR            ; the mapper registers' RAM copy, not
        ld      de,$DFFC                ; $FFFC-$FFFF: that would reach the mapper
        ld      bc,4
        ldir

        in      a,(VDP_CTRL)            ; reset the control-port pair
        ld      hl,HO_VDP
        ld      e,$80
        ld      b,11
vdp:
        ld      a,(hl)
        out     (VDP_CTRL),a
        ld      a,e
        out     (VDP_CTRL),a
        inc     hl
        inc     e
        djnz    vdp
        in      a,(VDP_CTRL)            ; drop a pending frame interrupt

        ld      a,$9F                   ; PSG: all four channels silent
        out     (PSG),a
        ld      a,$BF
        out     (PSG),a
        ld      a,$DF
        out     (PSG),a
        ld      a,$FF
        out     (PSG),a

        ld      a,(HO_3F)
        out     ($3F),a
        ld      a,(HO_3E)
        res     6,a                     ; never disable the slot we run from
        out     ($3E),a

        ld      sp,$DFF0
        im      1
        ld      (HOT_GO),a
        jp      $0000                   ; the cart flips on this fetch
