; fujidisp.inc -- drawing through the cart's text engine (s2_text.h): every
; call is a few hotspot reads; the cart renders into the raster it serves.
; FCALLed; may clobber R8, R9.

; d_at -- cursor to D = row * 16 + column.
d_at:   HOT     TX_CURSOR
        FRET

; d_putc -- draw D at the cursor and advance.
d_putc: HOT     TX_PUTC
        FRET

; d_op -- operation D (TOP_*), taking the parameters pushed so far.
d_op:   HOT     TX_OP
        FRET

; d_par -- push parameter D.
d_par:  HOT     TX_PARAM
        FRET

; d_cls -- clear the screen, cursor home.
d_cls:  LDI     TOP_CLS
        HOT     TX_OP
        FRET

; d_puts -- draw the NUL-terminated string at R8. Leaves R8 past the NUL.
        FIT     16
d_puts: LDI     TX_PUTC
        PHI     RF
dps1:   LDA     R8
        BZ      dps2
        PLO     RF
        LDN     RF
        BR      dps1
dps2:   LDI     FN_PARK
        PHI     RF
        FRET

; d_hex -- draw D as two hex digits.
        FIT     32
d_hex:  PLO     R9
        SHR
        SHR
        SHR
        SHR
        FCALL   dhx1
        GLO     R9
        ANI     0FH
dhx1:   SMI     10
        BDF     dhx2
        ADI     '0'+10
        BR      dhx3
dhx2:   ADI     'A'
dhx3:   HOT     TX_PUTC
        FRET

; d_rpl -- print reply text from offset R8, at most D characters (0: up to
; the NUL), without crossing the CPU: the cart reads its own reply window.
        FIT     48
d_rpl:  PLO     R9
        LDI     TOP_PRINT
        BR      drp1
; d_rplpad -- the same, then blanks out to D characters.
d_rplpad:
        PLO     R9
        LDI     TOP_PRINT_PAD
drp1:   PHI     R9
        GLO     R8
        HOT     TX_PARAM
        GHI     R8
        HOT     TX_PARAM
        GLO     R9
        HOT     TX_PARAM
        GHI     R9
        HOT     TX_OP
        FRET
