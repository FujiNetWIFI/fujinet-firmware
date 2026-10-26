; s2gfx.asm -- protocol 2's probe: the bank window, pokes, COPY, and the
; graphics layer (images, stamps, rectangles, pixel text, 16 sprites, the
; VIEW). Assembled once per bank (-D BANK=n); mkst2.py takes bank 1's
; window page from the second object. emu/gfxprobe.lua checks the screen.

; BANKS: 2

        CPU     1802
        INCLUDE "fujinet.inc"
        INCLUDE "s2macro.inc"
        IFNDEF  BANK
BANK    EQU     0
        ENDIF

; Parameters with RF.1 on TX_PARAM: three instructions each.
PARS    MACRO
        LDI     TX_PARAM
        PHI     RF
        ENDM
PAR     MACRO   val
        LDI     val
        PLO     RF
        LDN     RF
        ENDM
GOP     MACRO   code
        LDI     TX_OP
        PHI     RF
        LDI     code
        PLO     RF
        LDN     RF
        LDI     FN_PARK
        PHI     RF
        ENDM
REG     MACRO   num, val
        LDI     num
        HOT     HI(FN_REGSEL)
        LDI     val
        HOT     HI(FN_REGDATA)
        ENDM

        ORG     0400H
        INCLUDE "s2call.inc"
        INCLUDE "fujidisp.inc"

        ORG     0C00H
main:   FCALL   d_cls
        LDI     00H
        FCALL   d_at
        LDR     R8, t_proto
        FCALL   d_puts
        PEEK    FN_PROTOVER
        FCALL   d_hex

; Banks: $C400 answers B0 in bank 0, B1 in bank 1; bank 2 is not there.
        LDI     10H
        FCALL   d_at
        LDR     R8, t_bank
        FCALL   d_puts
        FCALL   0C400H
        FCALL   d_hex
        LDI     LO(FN_BANKSEL)+1
        HOT     HI(FN_BANKSEL)
        FCALL   0C400H
        PHI     RA
        LDI     LO(FN_BANKSEL)+2
        HOT     HI(FN_BANKSEL)
        PEEK    0C400H
        PLO     RA
        LDI     LO(FN_BANKSEL)
        HOT     HI(FN_BANKSEL)
        LDI     ' '
        FCALL   d_putc
        GHI     RA
        FCALL   d_hex
        LDI     ' '
        FCALL   d_putc
        GLO     RA
        FCALL   d_hex

; Pokes: "PK!" into the image's $D400, read straight back.
        LDI     20H
        FCALL   d_at
        REG     FN_REG_PTRLO, 00H
        REG     FN_REG_PTRHI, 0D4H
        LDI     FN_REG_POKE
        HOT     HI(FN_REGSEL)
        LDI     'P'
        HOT     HI(FN_REGDATA)
        LDI     'K'
        HOT     HI(FN_REGDATA)
        LDI     '!'
        HOT     HI(FN_REGDATA)
        LDI     0
        HOT     HI(FN_REGDATA)
        LDR     R8, 0D400H
        FCALL   d_puts

; COPY: t_copy to $D410, once FN_COPYDONE moves.
        LDI     30H
        FCALL   d_at
        PEEK    FN_COPYDONE
        PLO     RA
        REG     FN_REG_CPSRCLO, LO(t_copy)
        REG     FN_REG_CPSRCHI, HI(t_copy)
        REG     FN_REG_CPDSTLO, 10H
        REG     FN_REG_CPDSTHI, 0D4H
        REG     FN_REG_CPLENLO, 7
        REG     FN_REG_CPLENHI, 0
        FCALL   cpwait
        LDR     R8, 0D410H
        FCALL   d_puts
        FCALL   gfx
        FJMP    run

        FIT     24
cpwait: GLO     RA
        STR     R2
cpw1:   PEEK    FN_COPYDONE
        XOR
        BZ      cpw1
        FRET

; The graphics: a ring (image 0) and a ship (image 1); stamps by every mode
; between two dashed lines, pixel text, and 16 sprites.
gfx:    PARS
        PAR    0
        PAR    3
        PAR    3
        PAR    0E0H
        PAR    0A0H
        PAR    0E0H
        GOP     TOP_IMAGE
        PARS
        PAR    1
        PAR    8
        PAR    5
        PAR    18H
        PAR    3CH
        PAR    0FFH
        PAR    3CH
        PAR    18H
        GOP     TOP_IMAGE
        PARS
        PAR    0
        PAR    46
        PAR    64
        PAR    1
        PAR    TM_SET|TP_DASH
        GOP     TOP_RECT
        PARS
        PAR    0
        PAR    64
        PAR    64
        PAR    1
        PAR    TM_SET|TP_DASH
        GOP     TOP_RECT
        PARS
        PAR    2
        PAR    50
        PAR    TM_SET
        GOP     TOP_RECT            ; too few: nothing
        PARS
        PAR    1
        PAR    2
        PAR    50
        PAR    TM_OR
        GOP     TOP_STAMP
        PARS
        PAR    1
        PAR    11
        PAR    50
        PAR    TM_XOR
        GOP     TOP_STAMP
        PARS
        PAR    1
        PAR    20
        PAR    50
        PAR    TM_SET
        GOP     TOP_STAMP
        PARS
        PAR    41
        PAR    49
        PAR    TM_OR
        PAR    'G'
        PAR    'F'
        PAR    'X'
        GOP     TOP_PTEXT
        LDR     RC, sprtab
        LDI     0
        PLO     RA
        FIT     40
gfx1:   PARS
        GLO     RA
        PLO     RF
        LDN     RF                  ; n
        PAR    0                   ; the ring
        LDA     RC
        PLO     RF
        LDN     RF                  ; x
        LDA     RC
        PLO     RF
        LDN     RF                  ; y
        PAR    TM_OR
        GOP     TOP_SPRITE
        INC     RA
        GLO     RA
        XRI     16
        BNZ     gfx1
        FRET

; Every frame: the sprites step right (t & 7), and the picture shifts one
; pixel every other 64 frames.
        FIT     96
run:    GLO     RE
        STR     R2
        GLO     RB
        XOR
        BZ      run
        GLO     RE
        PLO     RB
        ANI     7
        PHI     RA
        LDR     RC, sprtab
        LDI     0
        PLO     RA
run1:   PARS
        GLO     RA
        PLO     RF
        LDN     RF                  ; n
        GHI     RA
        STR     R2
        LDA     RC
        ADD
        PLO     RF
        LDN     RF                  ; x0 + (t & 7)
        LDA     RC
        PLO     RF
        LDN     RF                  ; y
        GOP     TOP_SPRMOVE
        INC     RA
        GLO     RA
        XRI     16
        BNZ     run1
        PARS
        GLO     RB
        SHR
        SHR
        SHR
        SHR
        SHR
        SHR
        ANI     1
        PLO     RF
        LDN     RF                  ; dx
        PAR    0
        PAR    0
        GOP     TOP_VIEW
        BR      run

; Sprite n: x0, y. Two rows of eight, seven pixels apart.
sprtab: DB      0, 80, 7, 80, 14, 80, 21, 80, 28, 80, 35, 80, 42, 80, 49, 80
        DB      0, 88, 7, 88, 14, 88, 21, 88, 28, 88, 35, 88, 42, 88, 49, 88

t_proto: DB     "PROTO ", 0
t_bank:  DB     "BANK ", 0
t_copy:  DB     "COPIED", 0

; The window: one entry, which says which bank it is.
        ORG     0C400H
        LDI     0B0H+BANK
        FRET

; RAM: a page of the image the probe writes.
        ORG     0D400H
        REPT    256
        DB      0
        ENDM

        S2CLAIM
