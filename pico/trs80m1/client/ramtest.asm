;=====================================================================
; RAMTEST.ASM -- the card's 32K at 8000h-FFFFh: two passes of an address
; pattern and its complement over every byte. Prints RAMTEST PASS 32K,
; RAMTEST NONE when nothing answers there, or RAMTEST FAIL AT <addr>.
;=====================================================================

        ORG     5200h

START:  DI
        LD      SP,STACK
        CALL    CLS
        CALL    PRINT
        DEFB    "RAMTEST: 8000H-FFFFH",13,0
        LD      HL,8000h        ; anything there at all?
        LD      (HL),0A5h
        LD      A,(HL)
        CP      0A5h
        JR      NZ,NONE
        LD      (HL),5Ah
        LD      A,(HL)
        CP      5Ah
        JR      NZ,NONE
        XOR     A
        CALL    PASS
        LD      A,0FFh
        CALL    PASS
        CALL    PRINT
        DEFB    "RAMTEST PASS 32K",13,0
        JP      EXIT

NONE:   CALL    PRINT
        DEFB    "RAMTEST NONE",13,0
        JP      EXIT

; Writes (L xor H xor A) everywhere, then reads it all back.
PASS:   LD      C,A
        LD      HL,8000h
PASS1:  LD      A,L
        XOR     H
        XOR     C
        LD      (HL),A
        INC     HL
        LD      A,H
        OR      L
        JR      NZ,PASS1
        LD      HL,8000h
PASS2:  LD      A,L
        XOR     H
        XOR     C
        CP      (HL)
        JR      NZ,BAD
        INC     HL
        LD      A,H
        OR      L
        JR      NZ,PASS2
        RET

BAD:    CALL    PRINT
        DEFB    "RAMTEST FAIL AT ",0
        LD      A,H
        CALL    PHEX
        LD      A,L
        CALL    PHEX
        CALL    CRLF
        JP      EXIT

        INCLUDE "m1con.inc"

        DEFS    128
STACK:
