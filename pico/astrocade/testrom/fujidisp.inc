; fujidisp.inc -- shared screen helpers for the FujiNet clients.
;
; Expects LINBUF/HEXBUF/OPTFB from the including file. STRDIS register form
; (Nutting p.37): HL = string, C = options, D = Y, E = X.

; Copy B bytes from HL into LINBUF, 0-terminated, sanitized for STRDIS:
; lowercase folds to uppercase, anything else outside 20H-63H becomes '.'
; (STRDIS treats 64H-7FH as control codes). Stops early at a 0 byte.
CPYSAN: LD      DE,LINBUF
CPYS1:  LD      A,(HL)
        OR      A
        JR      Z,CPYS9
        CP      61H             ; 'a'
        JR      C,CPYS2
        CP      7BH             ; 'z'+1
        JR      NC,CPYS2
        SUB     20H
CPYS2:  CP      20H
        JR      C,CPYS3
        CP      64H
        JR      C,CPYS4
CPYS3:  LD      A,'.'
CPYS4:  LD      (DE),A
        INC     HL
        INC     DE
        DJNZ    CPYS1
CPYS9:  XOR     A
        LD      (DE),A
        RET

; Display LINBUF at x=2, y=A.
SHOWLIN:
        LD      D,A
        LD      E,2
        LD      C,OPTFB
        LD      HL,LINBUF
        SYSTEM  STRDIS
        RET

; Display A as two hex digits at x=E, y=D.
HEXAT:  PUSH    DE
        LD      C,A
        RRCA
        RRCA
        RRCA
        RRCA
        CALL    HEXDIG
        LD      (HEXBUF),A
        LD      A,C
        CALL    HEXDIG
        LD      (HEXBUF+1),A
        XOR     A
        LD      (HEXBUF+2),A
        POP     DE
        LD      C,OPTFB
        LD      HL,HEXBUF
        SYSTEM  STRDIS
        RET

HEXDIG: AND     0FH
        ADD     A,'0'
        CP      '9'+1
        RET     C
        ADD     A,'A'-'9'-1
        RET
