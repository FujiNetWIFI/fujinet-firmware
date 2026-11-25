;=====================================================================
; PRNTEST.ASM -- a line to the FujiNet printer through the card's
; printer port at 37E8h, polled as the Level II ROM's driver polls it.
;=====================================================================

        ORG     5200h

PRINTER EQU     37E8h

START:  DI
        LD      SP,STACK
        CALL    CLS
        LD      HL,LINE
PR1:    LD      A,(HL)
        OR      A
        JR      Z,PR3
PR2:    LD      A,(PRINTER)     ; selected, no fault, paper, not busy
        AND     0F0h
        CP      30h
        JR      NZ,PR2
        LD      A,(HL)
        LD      (PRINTER),A
        INC     HL
        JR      PR1
PR3:    CALL    PRINT
        DEFB    "PRNTEST SENT",13,0
        JP      EXIT

LINE:   DEFB    "FUJINET MODEL I PRINTER TEST",13,0

        INCLUDE "m1con.inc"

        DEFS    64
STACK:
