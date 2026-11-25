;=====================================================================
; FNHELLO.ASM -- find the FujiNet Model I card and make one FujiBus round
; trip. Prints FNHELLO PASS or FNHELLO FAIL <step>.
;=====================================================================

        ORG     5200h

START:  DI
        LD      SP,STACK
        CALL    CLS
        CALL    PRINT
        DEFB    "FNHELLO: ",0
        CALL    PINIT
        LD      A,'1'
        JP      C,FAIL
        CALL    PRINT
        DEFB    "CARD AT ",0
        LD      A,(PBASE)
        CALL    PHEX
        CALL    PRINT
        DEFB    "H",13,0
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
        DEFB    "FUJINET ",0
        LD      HL,RXBUF+6+125  ; fn_version
        LD      B,15
        CALL    PNSTR
        CALL    CRLF
        CALL    PRINT
        DEFB    "FNHELLO PASS",13,0
        JP      EXIT

FAIL:   PUSH    AF
        CALL    PRINT
        DEFB    13,"FNHELLO FAIL ",0
        POP     AF
        CALL    CONOUT
        CALL    CRLF
        JP      EXIT

        INCLUDE "m1con.inc"
        INCLUDE "pipe.inc"
        INCLUDE "fnbus.inc"

TXBUF:  DEFS    400
RXBUF:  DEFS    768
        DEFS    128
STACK:
