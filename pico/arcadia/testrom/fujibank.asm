; fujibank.asm -- protocol 2 probe for the Emerson Arcadia 2001 FujiNet cart.
;
; An APPBANK image (8K + 3 pages = 14336 bytes) that checks, on screen and in
; RAM for emu/bankprobe.lua:
;   RES+0..4   the id byte at $0FFF after selecting pages 0..4: 00 01 02 03 FF
;              (page 4 does not exist, so the window reads $FF)
;   RES+5      what a call to page 2's entry at $0800 returned: C2
;   RES+6      01 when a poke stream into RAM-L ($0600) reads back
;   RES+7      01 when a poke stream into RAM-H ($2AFC) reads back
;   RES+8      01 when stray REGDATAs after a closed stream stored nothing
;   RES+9      the protocol version: 02
;   RES+10     00 when 'F','N' are present
;   RES+11..14 the tick (lo, hi) before and after 60 vblanks
;   RES+15     5A: done
; then shows the live tick on row 9.
;
; Build: ../build.sh fujibank

        CPU     2650

RES     EQU     $18D0           ; results, 16 bytes (zone A)
RAMH    EQU     $2AFC           ; where the RAM-H stream lands
NFRAME  EQU     $18E0           ; vblank counter (the boot stub's area: free here)

        ORG     $0000
        BCTA    UN,START        ; cartridge header: jump past the
        DB      $17             ; RETC,UN interrupt guard at $0003

        ORG     $0020
        INCLUDE "fujidisp.inc"
        INCLUDE "fujilib.inc"

START:  EORZ    R0
        LPSU
        LODI,R0 $02             ; COM=1: logical compares
        LPSL
        LODI,R1 0               ; RESET leaves the page alone: take page 0
        LODA,R0 *FBNKP,R1
        BSTA,UN DINIT
        BSTA,UN DCLS
        SETSTR  TSTR
        LODI,R2 $FF
        BSTA,UN DPRINT

        LODI,R3 15              ; clear the results
        EORZ    R0
CLR1:   STRA,R0 RES,R3
        BDRR,R3 CLR1
        STRA,R0 RES

; ---- 1. page ids, page 4 missing --------------------------------------
        LODI,R3 0
IDS1:   LODZ    R3
        STRZ    R1
        LODA,R0 *FBNKP,R1       ; select page R1: the next fetch sees it
        LODA,R0 $0FFF
        STRA,R0 RES,R3
        ADDI,R3 1
        COMI,R3 5
        BCFR,EQ IDS1

; ---- 2. a call into page 2's entry table -------------------------------
        LODI,R1 2
        LODA,R0 *FBNKP,R1
        BSTA,UN $0800
        STRA,R0 RES+5
        LODI,R1 0
        LODA,R0 *FBNKP,R1

; ---- 3. RAM-L and RAM-H --------------------------------------------------
        LODI,R1 FRRAMP0         ; pointer = 0x000 ($0600)
        LODI,R2 0
        BSTA,UN FNREGWR
        LODI,R1 FRRAMPK         ; stream A1 A2 A3 A4
        LODA,R0 *FSELP,R1
        LODI,R1 $A1
RAML1:  LODA,R0 *FDATP,R1
        ADDI,R1 1
        COMI,R1 $A5
        BCFR,EQ RAML1
        LODI,R1 FRRAMP3         ; pointer = 0x3FC ($2AFC)
        LODI,R2 $FC
        BSTA,UN FNREGWR
        LODI,R1 FRRAMPK
        LODA,R0 *FSELP,R1
        LODI,R1 $B1
        LODA,R0 *FDATP,R1
        LODI,R1 $B2
        LODA,R0 *FDATP,R1
        LODI,R1 FRRAMP0         ; close the burst
        LODA,R0 *FSELP,R1

        LODI,R3 0               ; RAM-L reads back directly (page 0)
        LODI,R2 $A1
RAML2:  LODA,R0 $0600,R3
        COMZ    R2
        BCFR,EQ RAML3
        ADDI,R2 1
        ADDI,R3 1
        COMI,R3 4
        BCFR,EQ RAML2
        LODI,R0 1
        STRA,R0 RES+6
RAML3:  LODA,R0 *FRAMHP         ; RAM-H through a pointer (page 1)
        COMI,R0 $B1
        BCFR,EQ RAMH1
        LODI,R1 1
        LODA,R0 *FRAMHP,R1
        COMI,R0 $B2
        BCFR,EQ RAMH1
        LODI,R0 1
        STRA,R0 RES+7

; ---- 4. strays after the close store nothing ----------------------------
RAMH1:  LODI,R1 $55             ; moves the pointer only
        LODA,R0 *FDATP,R1
        LODI,R1 $66             ; goes on to fujimail: nothing is armed there
        LODA,R0 *FDATP,R1
        LODA,R0 $0655
        BCFR,EQ STR1
        LODA,R0 $0666
        BCFR,EQ STR1
        LODA,R0 $0604           ; just past the stream
        BCFR,EQ STR1
        LODI,R0 1
        STRA,R0 RES+8

; ---- 5. version and magic ------------------------------------------------
STR1:   LODA,R0 *FPVERP
        STRA,R0 RES+9
        BSTA,UN FNCHECK
        STRA,R0 RES+10

; ---- 6. the tick over 60 vblanks -----------------------------------------
        BSTA,UN TICKRD
        STRA,R1 RES+11
        STRA,R2 RES+12
        LODI,R0 60
        STRA,R0 NFRAME
TK1:    TPSU    $80             ; wait out this vblank...
        BCTR,EQ TK1
TK2:    TPSU    $80             ; ...and for the next one
        BCFR,EQ TK2
        LODA,R0 NFRAME
        SUBI,R0 1
        STRA,R0 NFRAME
        BCFR,EQ TK1
        BSTA,UN TICKRD
        STRA,R1 RES+13
        STRA,R2 RES+14
        LODI,R0 $5A
        STRA,R0 RES+15

; ---- show the results, then the live tick --------------------------------
        LODI,R3 0
SHOW1:  LODA,R0 RES,R3
        STRZ    R1
        LODZ    R3              ; cell = 32 + 2 * index - 1
        ADDZ    R3
        ADDI,R0 $20-1
        STRZ    R2
        LODZ    R1
        BSTA,UN DHEX
        ADDI,R3 1
        COMI,R3 16
        BCFR,EQ SHOW1
LIVE:   BSTA,UN TICKRD
        LODZ    R1
        STRZ    R3              ; DHEX keeps R3: the low byte waits there
        LODZ    R2
        LODI,R2 $90-1
        BSTA,UN DHEX
        LODZ    R3
        BSTA,UN DHEX
        BCTA,UN LIVE

; Read the tick tear-free: R1 = lo, R2 = hi. Leaf. Clobbers R0.
TICKRD: LODA,R0 *FTKHP
        STRZ    R2
        LODA,R0 *FTKLP
        STRZ    R1
        LODA,R0 *FTKHP
        COMZ    R2
        BCFR,EQ TICKRD
        RETC,UN

TSTR:   DB      "FUJIBANK",0

FBNKP:  DWBE    FNBANK
FPVERP: DWBE    FNPVER
FTKLP:  DWBE    FNTIKL
FTKHP:  DWBE    FNTIKH
FRAMHP: DWBE    RAMH

; ---- page 0, in the base image ---------------------------------------------
        ORG     $0800
        LODI,R0 $C0
        RETC,UN
        ORG     $0FFF
        DB      0

; ---- pages 1-3: the flat's $3000+, folded on by mkimage.py --pages ---------
BANKPG  MACRO   n
        ORG     $3000+((n)-1)*$800
        PHASE   $0800
        LODI,R0 $C0+(n)
        RETC,UN
        DS      $0FFF-$
        DB      n
        DEPHASE
        ENDM

        BANKPG  1
        BANKPG  2
        BANKPG  3
