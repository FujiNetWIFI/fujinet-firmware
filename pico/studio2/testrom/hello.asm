; hello.asm -- M0: the display path. The cart's text engine draws into the
; raster it serves to the 1861's DMA; this client's own ISR points R0 there.
; Shows the frame counter (the ISR runs) and the last key pressed (input.inc).

        CPU     1802
        INCLUDE "fujinet.inc"
        INCLUDE "s2macro.inc"

        ORG     0400H
        INCLUDE "s2call.inc"
        INCLUDE "fujidisp.inc"
        INCLUDE "input.inc"

        FIT     96
main:   FCALL   d_cls
        LDI     01H
        FCALL   d_at
        LDR     R8, t_hello
        FCALL   d_puts
        LDI     21H
        FCALL   d_at
        LDR     R8, t_lower
        FCALL   d_puts
        LDI     41H
        FCALL   d_at
        LDR     R8, t_syms
        FCALL   d_puts
        LDI     61H
        FCALL   d_at
        LDR     R8, t_frame
        FCALL   d_puts
        LDI     81H
        FCALL   d_at
        LDR     R8, t_key
        FCALL   d_puts
loop:   LDI     68H
        FCALL   d_at
        GHI     RE
        FCALL   d_hex
        GLO     RE
        FCALL   d_hex
        FCALL   in_get
        BZ      loop
        PLO     RA
        LDI     88H
        FCALL   d_at
        GLO     RA
        FCALL   d_hex
        LDI     40                      ; a beep: 40 frames of Q
        PLO     RD
        BR      loop

t_hello: DB     "HELLO STUDIO II", 0
t_lower: DB     "fujinet 0-9", 0
t_syms:  DB     SYM_SPADE, SYM_HEART, SYM_DIAMOND, SYM_CLUB, " ", SYM_UP, SYM_DOWN, SYM_LEFT, SYM_RIGHT, " ", SYM_TEN, SYM_BACK, SYM_BLOCK, 0
t_frame: DB     "FRAME", 0
t_key:   DB     "KEY", 0

        S2CLAIM
