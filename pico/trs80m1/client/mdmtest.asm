;=====================================================================
; MDMTEST.ASM -- the FujiNet modem through the card's RS-232 board at
; E8h-EBh, the way terminal software uses a modem.
;
; 1 ATNET0 (raw TCP)  2 ATDT 127.0.0.1:8047, run.sh's `; echo` server
; 3 64 bytes, every SLIP special
; among them, echo back intact  4 +++ and ATH
; The rs232 FujiNet modem takes each command as one whole line and answers
; nothing in command mode, so commands go out a line at a time and the dial
; is given time rather than awaited.
; Prints MDMTEST PASS or MDMTEST FAIL <step>.
;=====================================================================

        ORG     5200h

START:  DI
        LD      SP,STACK
        CALL    CLS
        CALL    PRINT
        DEFB    "MDMTEST",13,0
        CALL    PINIT           ; for the card's millisecond TICK
        LD      A,'0'
        JP      C,FAIL
        CALL    UINIT
        LD      DE,500
        CALL    PWAIT

        LD      HL,ATNET        ; 1
        CALL    MSTR
        LD      DE,300
        CALL    PWAIT

        LD      HL,ATDT         ; 2
        CALL    MSTR
        LD      DE,1500         ; the dial
        CALL    PWAIT
        CALL    DRAIN

        LD      HL,PATTERN      ; 3
        LD      B,64
SEND1:  LD      A,(HL)
        CALL    MPUT
        INC     HL
        DEC     B
        JP      NZ,SEND1
        LD      HL,PATTERN
        LD      B,64
RECV1:  LD      DE,5000
        CALL    MGET
        LD      C,A
        LD      A,'3'
        JP      C,FAIL
        LD      A,C
        CP      (HL)
        LD      A,'4'
        JP      NZ,FAIL
        INC     HL
        DEC     B
        JP      NZ,RECV1
        CALL    PRINT
        DEFB    "ECHO INTACT, C0H/DBH INCLUDED",13,0

        LD      DE,1200         ; 4: guard time, +++, guard time, hang up
        CALL    PWAIT
        LD      A,'+'
        CALL    MPUT
        CALL    MPUT
        CALL    MPUT
        LD      DE,1200
        CALL    PWAIT
        LD      HL,ATH
        CALL    MSTR
        LD      DE,300
        CALL    PWAIT

        CALL    PRINT
        DEFB    "MDMTEST PASS",13,0
        JP      EXIT

FAIL:   PUSH    AF
        CALL    PRINT
        DEFB    13,"MDMTEST FAIL ",0
        POP     AF
        CALL    CONOUT
        CALL    CRLF
        JP      EXIT

; Send the NUL-ended string at HL to the modem.
MSTR:   LD      A,(HL)
        OR      A
        RET     Z
        CALL    MPUT
        INC     HL
        JP      MSTR

; Throw away anything the modem has already said.
DRAIN:  LD      DE,50
        CALL    MGET
        JP      NC,DRAIN
        RET

ATNET:  DEFB    "ATNET0",13,0
ATDT:   DEFB    "ATDT127.0.0.1:8047",13,0
ATH:    DEFB    "ATH",13,0
PATTERN:
        DEFB    0C0h,0DBh,0DCh,0DDh,00h,0FFh,0C0h,0C0h
        DEFB    "The quick brown fox jumps over the lazy dog "
        DEFB    0DBh,0C0h,0DBh,0DDh,01h,02h,03h,0C0h
        DEFB    13,10,"!",0C0h

        INCLUDE "m1con.inc"
        INCLUDE "pipe.inc"
        INCLUDE "m1uart.inc"

        DEFS    128
STACK:
