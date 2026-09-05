; hello.asm -- Arcadia 2001 toolchain + display smoke test. No FujiNet.
;
; Paints the full 64-glyph UVI character set, a user-defined character, a
; block-graphics-mode probe row, and echoes the keypad columns live as hex
; digits (the charset conveniently runs 0-9 then A-Z, so $10+nibble is a
; hex digit for free).
;
; Screen: row 0 title, rows 2-5 the charset, row 7 keypad echo, row 9 the
; UDC, row 11 the $C0/$40 graphics-mode probe.

        CPU     2650

SCREEN  EQU     $1800           ; upper screen, 16x13
UDC     EQU     $1980           ; user-defined characters, 8x8 bytes
GFXMOD  EQU     $19F8           ; bit7 gfx, bit6 26-line, 0-5 alt colours
BGCOL   EQU     $19F9           ; bit7 char size, 3-5 char col, 0-2 screen col
PITCH   EQU     $18FD           ; bit7 multicolor, 0-6 sound freq
VOLUME  EQU     $18FE           ; 0-2 loudness, 3 snd en, 5-7 horiz shift

        ORG     $0000
        BCTA    UN,START        ; cartridge header: jump past the
        DB      $17             ; RETC,UN interrupt guard at $0003

        ORG     $0020
START:  EORZ    R0
        LPSU                    ; power-on-like PSW
        LPSL

; --- UVI setup: 13-line text mode, white on black, sound off -------------
        EORZ    R0
        STRA,R0 GFXMOD          ; no gfx mode, 13 lines
        STRA,R0 PITCH
        STRA,R0 VOLUME
        LODI,R0 $87             ; 8x8 chars, char colour 0 (white),
        STRA,R0 BGCOL           ; screen colour 7 (black)

; --- clear the upper screen ($1800-$18CF, 208 cells) ---------------------
        LODI,R3 207
CLRLP:  EORZ    R0
        STRA,R0 SCREEN,R3
        BDRR,R3 CLRLP
        EORZ    R0
        STRA,R0 SCREEN

; --- title row ------------------------------------------------------------
        LODI,R3 12
TTLLP:  LODA,R0 MSG,R3
        STRA,R0 SCREEN,R3
        BDRR,R3 TTLLP
        LODA,R0 MSG
        STRA,R0 SCREEN

; --- rows 2-5: the whole character set, codes $00-$3F ---------------------
        LODI,R3 63
CHRLP:  LODZ    R3
        STRA,R0 SCREEN+32,R3
        BDRR,R3 CHRLP
        EORZ    R0
        STRA,R0 SCREEN+32

; --- a UDC in slot $38 (asymmetric, to expose the byte layout) ------------
        LODI,R3 7
UDCLP:  LODA,R0 UDCDAT,R3
        STRA,R0 UDC,R3
        BDRR,R3 UDCLP
        LODA,R0 UDCDAT
        STRA,R0 UDC
        LODI,R0 $38             ; show it at row 9
        STRA,R0 SCREEN+144

; --- row 11: graphics-mode probe ($C0 flips the row, $40 flips it back) ---
        LODI,R3 9
GFXLP:  LODA,R0 GFXPRB,R3
        STRA,R0 SCREEN+176,R3
        BDRR,R3 GFXLP
        LODA,R0 GFXPRB
        STRA,R0 SCREEN+176

; --- main loop: echo keypad columns + panel as hex at row 7 ---------------
MAIN:   TPSU    $80             ; wait for vertical retrace start
        BCFR,EQ MAIN

        LODA,R0 $1900           ; P1 column 1 (1/4/7/Clear)
        ANDI,R0 $0F
        IORI,R0 $10
        STRA,R0 SCREEN+112
        LODA,R0 $1901           ; P1 column 2 (2/5/8/0 + fire)
        ANDI,R0 $0F
        IORI,R0 $10
        STRA,R0 SCREEN+114
        LODA,R0 $1902           ; P1 column 3 (3/6/9/Enter)
        ANDI,R0 $0F
        IORI,R0 $10
        STRA,R0 SCREEN+116
        LODA,R0 $1908           ; panel (Start/Option/Select)
        ANDI,R0 $0F
        IORI,R0 $10
        STRA,R0 SCREEN+118

MAINW:  TPSU    $80             ; wait for retrace end
        BCTR,EQ MAINW
        BCTA    UN,MAIN

; --- data -----------------------------------------------------------------
MSG:    DB      $21,$1E,$25,$25,$28,$00         ; HELLO
        DB      $1F,$2E,$23,$22,$27,$1E,$2D     ; FUJINET

UDCDAT: DB      %10000000       ; asymmetric F-like shape: if rendered
        DB      %11111110       ; mirrored or byte-swapped it is obvious
        DB      %10000000
        DB      %11111100
        DB      %10000000
        DB      %10000000
        DB      %10000000
        DB      %00000000

GFXPRB: DB      $C0,$01,$02,$03,$3F,$40,$21,$1E,$25,$28
