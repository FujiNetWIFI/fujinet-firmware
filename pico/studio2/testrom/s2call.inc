; s2call.inc -- the runtime every Studio II FujiNet client starts with: the
; entry the BIOS calls, the display interrupt, SCRT, and the hand-over.
; Include at ORG $0400; it is under a page, so its short branches resolve.

; ---- entry ----
; The BIOS byte-code jumps to $0400; "04 02" there calls native $0402 with
; P = 3, IE on and the BIOS ISR live. Swap the vector with interrupts off.
        DB      04H, 02H
s2_entry:
        SEX     R3
        DIS                             ; X = 3, P = 3, IE off
        DB      33H
        LDR     R1, isr
        LDR     R2, STACK
        LDR     R4, scrt_call
        LDR     R5, scrt_ret
        LDR     R7, scrt_jump
        LDI     0
        PLO     RD
        PHI     RD
        PLO     RE
        PHI     RE
        LDI     FN_PARK
        PHI     RF
        LDR     R0, FN_RASTER
        SEX     R3
        RET                             ; X = 2, P = 3, IE on
        DB      23H
        FJMP    main

; ---- the display interrupt ----
; The 1861 raises INT two lines before its first DMA and holds it 28 cycles.
; Like the BIOS's: one LBR so the first DMA lands on an instruction boundary,
; then wait out EF1 (it drops at the first DMA) so INT is gone before RET.
; R0 points the 1024 DMA reads at the cart's raster, which serves them by
; position. RE counts frames; RD.0 is the beep, Q on while it counts down.
        FIT     32
isrx:   LDA     R2                      ; D back
        RET                             ; leaves R1 at isr
isr:    DEC     R2
        SAV                             ; T
        DEC     R2
        STR     R2                      ; D
        LBR     isr1                    ; !3CYCLE: before the first DMA
isr1:   LDI     HI(FN_RASTER)
        PHI     R0
        LDI     0
        PLO     R0
        INC     RE
        GLO     RD
        BZ      isrq
        DEC     RD
        SEQ
        BR      isrw
isrq:   REQ
isrw:   B1      isrw
        BR      isrx

; ---- SCRT ----
; FCALL: SEP R4, DW target. R6, the link, goes on the stack; D and DF survive.
        FIT     20
        SEP     R3
scrt_call:
        PHI     RD
        SEX     R2
        GHI     R6
        STXD
        GLO     R6
        STXD
        GHI     R3
        PHI     R6
        GLO     R3
        PLO     R6
        LDA     R6
        PHI     R3
        LDA     R6
        PLO     R3
        GHI     RD
        BR      scrt_call-1

; FRET: SEP R5.
        FIT     18
        SEP     R3
scrt_ret:
        PHI     RD
        GHI     R6
        PHI     R3
        GLO     R6
        PLO     R3
        SEX     R2
        IRX
        LDXA
        PLO     R6
        LDX
        PHI     R6
        GHI     RD
        BR      scrt_ret-1

; FJMP: SEP R7, DW target.
        FIT     15
        SEP     R3
scrt_jump:
        PHI     RD
        SEX     R2
        LDA     R3
        STXD
        LDN     R3
        PLO     R3
        IRX
        LDX
        PHI     R3
        GHI     RD
        BR      scrt_jump-1

; ---- the hand-over ----
; Start the BIOS over, exactly as a power-on would reach it, with the cart
; swapping the staged image in on the BIOS's first fetch. Call it once
; FN_ARMED is set. Runs in the ~1,850 cycles between the bottom of one frame
; and the next frame's INT: no DMA, no interrupt. Never returns.
        FIT     70
fn_handover:
        GLO     RE                      ; the next frame begins...
        STR     R2
hov1:   GLO     RE
        XOR
        BZ      hov1
hov2:   BN1     hov2                    ; ...and runs to EF1 at its bottom
hov3:   B1      hov3                    ; line 208: quiet until line 78
        SEX     R3
        DIS                             ; X = 3, P = 3, IE off
        DB      33H
        LDR     R8, 08FFH               ; RAM the BIOS does not clear
        SEX     R8
hov4:   LDI     0
        STXD
        GLO     R8
        XRI     9FH
        BNZ     hov4
        LDR     RB, hov5
        SEP     RB                      ; R3 is free from here
hov5:   LDR     R0, 0001H               ; R0.1 = 0 for the BIOS's GHI R0
        LDR     R1, 001CH
        LDR     R2, 08BFH
        LDI     08H
        PHI     R6
        PHI     R8
        LDI     0
        PLO     R3
        PHI     R3
        REQ
        LDR     RC, FN_STUB
        SEP     RC                      ; SEX RC; RET; $23: X=2, P=3 at $0000
