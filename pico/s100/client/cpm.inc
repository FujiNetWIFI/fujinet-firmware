;=====================================================================
; CPM.INC -- console output through the CP/M BDOS, 8080 only.
;
;   CONOUT  A to the console              PRINT   inline string, NUL ended
;   PSTR    string at HL, NUL ended       CRLF
;   PHEX    A as two hex digits           PDEC    HL in decimal
;   PNSTR   at most B chars of the string at HL (stops at NUL)
; All preserve BC, DE and HL unless stated.
;=====================================================================

BDOS    EQU     0005h

CONOUT: PUSH    AF
        PUSH    BC
        PUSH    DE
        PUSH    HL
        LD      E,A
        LD      C,2
        CALL    BDOS
        POP     HL
        POP     DE
        POP     BC
        POP     AF
        RET

CRLF:   LD      A,13
        CALL    CONOUT
        LD      A,10
        JP      CONOUT

; CALL PRINT / DEFB "text",0 -- returns past the string
PRINT:  EX      (SP),HL
        CALL    PSTR
        INC     HL
        EX      (SP),HL
        RET

; leaves HL on the NUL
PSTR:   LD      A,(HL)
        OR      A
        RET     Z
        CALL    CONOUT
        INC     HL
        JP      PSTR

PNSTR:  PUSH    BC
        PUSH    HL
PNS1:   LD      A,B
        OR      A
        JP      Z,PNS2
        LD      A,(HL)
        OR      A
        JP      Z,PNS2
        CALL    CONOUT
        INC     HL
        DEC     B
        JP      PNS1
PNS2:   POP     HL
        POP     BC
        RET

PHEX:   PUSH    AF
        RRCA
        RRCA
        RRCA
        RRCA
        CALL    PHEX1
        POP     AF
PHEX1:  PUSH    AF
        AND     0Fh
        ADD     A,90h
        DAA
        ADC     A,40h
        DAA
        CALL    CONOUT
        POP     AF
        RET

; HL unsigned, no leading zeroes
PDEC:   PUSH    BC
        PUSH    DE
        PUSH    HL
        LD      B,0             ; digits pushed
PDEC1:  LD      DE,10
        CALL    DIV16           ; HL = HL / 10, A = remainder
        PUSH    AF
        INC     B
        LD      A,H
        OR      L
        JP      NZ,PDEC1
PDEC2:  POP     AF
        ADD     A,'0'
        CALL    CONOUT
        DEC     B
        JP      NZ,PDEC2
        POP     HL
        POP     DE
        POP     BC
        RET

; HL = HL / DE, A = remainder (DE < 256)
DIV16:  PUSH    BC
        LD      B,16
        LD      C,0             ; remainder
DIV1:   ADD     HL,HL           ; shift dividend left, top bit into carry
        LD      A,C
        RLA
        LD      C,A
        SUB     E
        JP      C,DIV2
        LD      C,A
        INC     L               ; quotient bit
DIV2:   DEC     B
        JP      NZ,DIV1
        LD      A,C
        POP     BC
        RET
