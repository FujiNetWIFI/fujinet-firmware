;=====================================================================
; FNHELLO.ASM -- find the FujiNet S-100 card and make one FujiBus round
; trip. CP/M, 8080. Prints FNHELLO PASS or FNHELLO FAIL <step>.
;=====================================================================

        ORG     0100h

START:  LD      SP,STACK
        CALL    PRINT
        DEFB    "FNHELLO: ",0
        CALL    PINIT
        LD      A,'1'
        JP      C,FAIL
        CALL    PRINT
        DEFB    "card at ",0
        LD      A,(PBASE)
        CALL    PHEX
        CALL    PRINT
        DEFB    "h",13,10,0
        CALL    PFLUSH
        LD      A,70h           ; FUJI: GET_ADAPTERCONFIG_EXTENDED
        LD      C,0C4h
        LD      B,0
        CALL    FBNEW
        CALL    FBCALL
        LD      A,'2'
        JP      C,FAIL
        LD      A,'3'
        JP      NZ,FAIL
        CALL    PRINT
        DEFB    "FujiNet ",0
        LD      HL,RXBUF+6+125  ; fn_version
        LD      B,15
        CALL    PNSTR
        CALL    CRLF
        CALL    PRINT
        DEFB    "FNHELLO PASS",13,10,0
        JP      0

FAIL:   PUSH    AF
        CALL    PRINT
        DEFB    13,10,"FNHELLO FAIL ",0
        POP     AF
        CALL    CONOUT
        CALL    CRLF
        JP      0

        INCLUDE "cpm.inc"
        INCLUDE "fnport.inc"
        INCLUDE "fnbus.inc"

TXBUF:  DEFS    400
RXBUF:  DEFS    768
        DEFS    128
STACK:
