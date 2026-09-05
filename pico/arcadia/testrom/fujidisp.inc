; fujidisp.inc -- 2637 UVI display helpers for the Arcadia FujiNet clients.
;
; The UVI has a 64-glyph character generator (no lowercase, no / - _ :) and
; each screen byte is 2 colour bits + a 6-bit glyph code. Text is written
; straight into screen RAM; there is no BIOS. The missing path glyphs live
; in the 8 user-defined characters ($1980-$19BF, codes $38-$3F); sprites
; are unused by every client, so all 8 slots are free.
;
; Never emit colour bits 11 in a text cell: a $C0 byte flips the whole row
; into block-graphics mode. DXLAT can only produce codes $00-$3F.
;
; Uses the fujilib register conventions (R0 data, R1/R2 args, R3 counters).

SCREEN  EQU     $1800           ; upper screen, 16 cols x 13 rows
UDC     EQU     $1980           ; 8 user-defined characters, 8 bytes each
GFXMOD  EQU     $19F8           ; bit7 gfx mode, bit6 26-line, 0-5 alt cols
BGCOL   EQU     $19F9           ; bit7 char size, 6 pot mux, 3-5 char col,
                                ; 0-2 screen col (colours INVERTED: 0=white,
                                ; 7=black)
PITCH   EQU     $18FD           ; bit7 multicolor, 0-6 sound frequency
VOLUME  EQU     $18FE           ; 0-2 loudness, 3 snd en, 5-7 horiz shift

DSTRP   EQU     $18ED           ; DPRINT string pointer, big-endian word

SETSTR  MACRO   adr             ; point DPRINT at a string (clobbers R0)
        LODI,R0 (adr)>>8
        STRA,R0 DSTRP
        LODI,R0 (adr)&$FF
        STRA,R0 DSTRP+1
        ENDM

; UDC glyph codes (assignments shared by every client)
GSLASH  EQU     $38
GDASH   EQU     $39
GUSCOR  EQU     $3A
GCOLON  EQU     $3B
GCHEVR  EQU     $3C             ; cursor chevron
GBULLT  EQU     $3D             ; password mask
GMORE   EQU     $3E             ; more-pages hint
GBLOCK  EQU     $03             ; built-in solid block (progress bar fill)
GBLINE  EQU     $06             ; built-in baseline (progress bar empty)

; 13-line text mode, white on black, sound off, UDCs loaded.
; Calls one down (DUDCLD). Clobbers R0,R1,R3.
DINIT:  EORZ    R0
        STRA,R0 GFXMOD
        STRA,R0 PITCH
        STRA,R0 VOLUME
        LODI,R0 $87             ; 8x8 chars, white text, black screen
        STRA,R0 BGCOL
        LODI,R3 63              ; load the 8 UDCs (64 bytes)
DUDCLD: LODA,R0 UDCTAB,R3
        STRA,R0 UDC,R3
        BDRR,R3 DUDCLD
        LODA,R0 UDCTAB
        STRA,R0 UDC
        RETC,UN

; Clear the upper screen (208 cells to spaces). Leaf. Clobbers R0,R3.
DCLS:   LODI,R3 207
DCLS1:  EORZ    R0
        STRA,R0 SCREEN,R3
        BDRR,R3 DCLS1
        EORZ    R0
        STRA,R0 SCREEN
        RETC,UN

; ASCII in R0 -> glyph code in R0 ($00-$3F only). Lowercase folds to
; uppercase, anything unprintable becomes '.'. Leaf. Clobbers R0,R1.
DXLAT:  ANDI,R0 $7F
        SUBI,R0 $20
        BCTR,LT DXLBAD          ; control character
        STRZ    R1
        LODA,R0 XLAT,R1
        RETC,UN
DXLBAD: LODI,R0 $34             ; '.'
        RETC,UN

; Print the 0-terminated ASCII string at (DSTRP) starting at screen offset
; R2 + 1 (R2 = target cell minus one, the auto-increment convention; on
; return R2 is the last cell written). Stops at the 0 terminator; the
; caller owns line-length discipline. Calls one down (DXLAT).
; Clobbers R0,R1,R2,R3.
DPRINT: LODI,R3 $FF
DPRT1:  LODA,R0 *DSTRP,R3,+    ; str[++R3]; a load sets CC
        BCTR,EQ DPRT2
        BSTR,UN DXLAT
        STRA,R0 SCREEN,R2,+    ; screen[++R2]
        BCTR,UN DPRT1
DPRT2:  RETC,UN

; Print R0 as two hex digits at screen offset R2+1 (same convention).
; The charset makes this free: $10+nibble runs 0-9 then A-F. Leaf.
; Clobbers R0,R1,R2.
DHEX:   STRZ    R1
        RRR,R0                  ; high nibble down (4 rotates)
        RRR,R0
        RRR,R0
        RRR,R0
        ANDI,R0 $0F
        IORI,R0 $10
        STRA,R0 SCREEN,R2,+
        LODZ    R1
        ANDI,R0 $0F
        IORI,R0 $10
        STRA,R0 SCREEN,R2,+
        RETC,UN

; ---- keypad --------------------------------------------------------
; P1 keypad, active high, masked $0F, one column per address:
;   $1900 b3=1 b2=4 b1=7 b0=Clear
;   $1901 b3=2 b2=5 b1=8 b0=0
;   $1902 b3=3 b2=6 b1=9 b0=Enter
; The CONFIG UI uses: 2 up, 8 down, 4 page-up, 6 page-down, Enter select,
; Clear back.
KUP     EQU     1
KDOWN   EQU     2
KPGUP   EQU     3
KPGDN   EQU     4
KSEL    EQU     5
KBACK   EQU     6

; SCAN: R0 = code of the first key found (0 if none). Leaf. Clobbers R0.
SCAN:   LODA,R0 $1901           ; '2' up  (b3)
        ANDI,R0 $08
        BCFR,EQ SCU
        LODA,R0 $1901           ; '8' down (b1)
        ANDI,R0 $02
        BCFR,EQ SCD
        LODA,R0 $1902           ; Enter select (b0)
        ANDI,R0 $01
        BCFR,EQ SCS
        LODA,R0 $1900           ; Clear back (b0)
        ANDI,R0 $01
        BCFR,EQ SCB
        LODA,R0 $1900           ; '4' page up (b2)
        ANDI,R0 $04
        BCFR,EQ SCPU
        LODA,R0 $1902           ; '6' page down (b2)
        ANDI,R0 $04
        BCFR,EQ SCPD
        EORZ    R0
        RETC,UN
SCU:    LODI,R0 KUP
        RETC,UN
SCD:    LODI,R0 KDOWN
        RETC,UN
SCS:    LODI,R0 KSEL
        RETC,UN
SCB:    LODI,R0 KBACK
        RETC,UN
SCPU:   LODI,R0 KPGUP
        RETC,UN
SCPD:   LODI,R0 KPGDN
        RETC,UN

; GETKEY: block for a fresh keypress; R0 = key code. Waits for full release
; first so a held key steps once per press. Calls one down (SCAN).
GETKEY: BSTA,UN SCAN            ; wait for release
        COMI,R0 0
        BCFR,EQ GETKEY
GKW:    BSTA,UN SCAN            ; wait for a press
        COMI,R0 0
        BCTR,EQ GKW
        RETC,UN

; ---- ASCII $20-$7F -> glyph, 96 entries -------------------------------
XLAT:   DB      $00,$34,$34,$34,$37,$34,$34,$34 ; spc ! " # $ % & '
        DB      $34,$34,$34,$36,$35,GDASH,$34,GSLASH ; ( ) * + , - . /
        DB      $10,$11,$12,$13,$14,$15,$16,$17 ; 0-7
        DB      $18,$19,GCOLON,$34,$34,$34,$34,$34 ; 8 9 : ; < = > ?
        DB      $34,$1A,$1B,$1C,$1D,$1E,$1F,$20 ; @ A-G
        DB      $21,$22,$23,$24,$25,$26,$27,$28 ; H-O
        DB      $29,$2A,$2B,$2C,$2D,$2E,$2F,$30 ; P-W
        DB      $31,$32,$33,$34,$34,$34,$34,GUSCOR ; X Y Z [ \ ] ^ _
        DB      $34,$1A,$1B,$1C,$1D,$1E,$1F,$20 ; ` a-g
        DB      $21,$22,$23,$24,$25,$26,$27,$28 ; h-o
        DB      $29,$2A,$2B,$2C,$2D,$2E,$2F,$30 ; p-w
        DB      $31,$32,$33,$34,$34,$34,$34,$34 ; x y z { | } ~ del

; ---- the 8 UDCs, 8 bytes each ($38-$3F) --------------------------------
UDCTAB: DB      %00000010       ; $38 /
        DB      %00000100
        DB      %00001000
        DB      %00010000
        DB      %00100000
        DB      %01000000
        DB      %00000000
        DB      %00000000
        DB      %00000000       ; $39 -
        DB      %00000000
        DB      %00000000
        DB      %01111100
        DB      %00000000
        DB      %00000000
        DB      %00000000
        DB      %00000000
        DB      %00000000       ; $3A _
        DB      %00000000
        DB      %00000000
        DB      %00000000
        DB      %00000000
        DB      %00000000
        DB      %01111110
        DB      %00000000
        DB      %00000000       ; $3B :
        DB      %00011000
        DB      %00011000
        DB      %00000000
        DB      %00011000
        DB      %00011000
        DB      %00000000
        DB      %00000000
        DB      %01000000       ; $3C chevron cursor
        DB      %01100000
        DB      %01110000
        DB      %01111000
        DB      %01110000
        DB      %01100000
        DB      %01000000
        DB      %00000000
        DB      %00000000       ; $3D bullet (password mask)
        DB      %00111000
        DB      %01111100
        DB      %01111100
        DB      %00111000
        DB      %00000000
        DB      %00000000
        DB      %00000000
        DB      %00010000       ; $3E more-pages hint (up+down)
        DB      %00111000
        DB      %00000000
        DB      %00000000
        DB      %00000000
        DB      %00111000
        DB      %00010000
        DB      %00000000
        DB      %00000000       ; $3F spare
        DB      %00000000
        DB      %00000000
        DB      %00000000
        DB      %00000000
        DB      %00000000
        DB      %00000000
        DB      %00000000
