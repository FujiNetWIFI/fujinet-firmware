;=====================================================================
; M1CON.INC -- console output for the Model I clients, through the
; Level II ROM, so it works booted alone or under any DOS.
;
;   CLS     clear the screen
;   CONOUT  print A          PRINT   print the 0-terminated string after the CALL
;   CRLF    new line         PHEX    print A in hex
;   PNSTR   print B bytes at HL, stopping at a 0
;   PSTR    print the 0-terminated string at HL
;   EXIT    back to the DOS, or stop here when there is none
;=====================================================================

ROMOUT  EQU     0033h           ; display the character in A
ROMCLS  EQU     01C9h           ; clear the screen, cursor home
DOSRET  EQU     402Dh           ; a DOS's return; JP 5000h when there is no DOS

CLS:    PUSH    BC
        PUSH    DE
        PUSH    HL
        CALL    ROMCLS
        POP     HL
        POP     DE
        POP     BC
        RET

CONOUT: PUSH    BC
        PUSH    DE
        PUSH    HL
        CALL    ROMOUT
        POP     HL
        POP     DE
        POP     BC
        RET

CRLF:   LD      A,0Dh
        JP      CONOUT

PRINT:  EX      (SP),HL
PRIN1:  LD      A,(HL)
        INC     HL
        OR      A
        JR      Z,PRIN2
        CALL    CONOUT
        JR      PRIN1
PRIN2:  EX      (SP),HL
        RET

PHEX:   PUSH    AF
        RRCA
        RRCA
        RRCA
        RRCA
        CALL    PHEX1
        POP     AF
PHEX1:  AND     0Fh
        ADD     A,'0'
        CP      '9'+1
        JR      C,PHEX2
        ADD     A,7
PHEX2:  JP      CONOUT

PNSTR:  LD      A,(HL)
        OR      A
        RET     Z
        CALL    CONOUT
        INC     HL
        DJNZ    PNSTR
        RET

PSTR:   LD      A,(HL)
        OR      A
        RET     Z
        CALL    CONOUT
        INC     HL
        JR      PSTR

; A DOS has replaced the ROM's JP 5000h at 402Dh with its own return.
EXIT:   LD      HL,(DOSRET+1)
        LD      A,H
        CP      50h
        JR      NZ,EXIT1
        LD      A,L
        OR      A
        JR      NZ,EXIT1
EXIT0:  JR      EXIT0           ; no DOS: nowhere to go (HALT would be an NMI)
EXIT1:  JP      DOSRET
