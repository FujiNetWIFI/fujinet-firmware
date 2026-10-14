;=====================================================================
; PRNTEST.ASM -- a line to the FujiNet printer through the card's 2SIO
; channel B, as a CP/M LST: driver would send it. CP/M, 8080.
;=====================================================================

        ORG     0100h

START:  LD      SP,STACK
        CALL    PINIT
        LD      A,SIOBASE
        CALL    SINIT
        LD      HL,LINE
PR1:    LD      A,(HL)
        OR      A
        JP      Z,PR2
        CALL    LPUT
        INC     HL
        JP      PR1
PR2:    CALL    PRINT
        DEFB    "PRNTEST SENT",13,10,0
        JP      0

LINE:   DEFB    "FUJINET S-100 PRINTER TEST",13,10,0

        INCLUDE "cpm.inc"
        INCLUDE "fnport.inc"
        INCLUDE "sio.inc"

        DEFS    64
STACK:
