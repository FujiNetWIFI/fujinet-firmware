;=====================================================================
; FNTEST.ASM -- the basic FujiNet test for the Model I card.
;
; 1 find the card   2 adapter config (SSID, IP, MAC, version)
; 3 host slots      4 open N1:HTTP://127.0.0.1:8046/FNTEST.TXT, which
;                     run.sh's `; http` serves
; 5 read it         6 compare it with the expected text
; Prints FNTEST PASS or FNTEST FAIL <step>.
;=====================================================================

        ORG     5200h

FUJI    EQU     70h
NET1    EQU     71h

START:  DI
        LD      SP,STACK
        CALL    CLS
        CALL    PRINT
        DEFB    "FNTEST",13,0

        CALL    PINIT
        LD      A,'1'
        JP      C,FAIL
        CALL    PFLUSH

        LD      A,FUJI          ; GET_ADAPTERCONFIG_EXTENDED
        LD      C,0C4h
        LD      B,0
        CALL    FBNEW
        CALL    FBCALL
        LD      A,'2'
        JP      C,FAIL
        JP      NZ,FAIL
        CALL    PRINT
        DEFB    "SSID:    ",0
        LD      HL,RXBUF+6
        LD      B,32
        CALL    PNSTR
        CALL    PRINT
        DEFB    13,"IP:      ",0
        LD      HL,RXBUF+6+140
        LD      B,16
        CALL    PNSTR
        CALL    PRINT
        DEFB    13,"MAC:     ",0
        LD      HL,RXBUF+6+204
        LD      B,18
        CALL    PNSTR
        CALL    PRINT
        DEFB    13,"VERSION: ",0
        LD      HL,RXBUF+6+125
        LD      B,15
        CALL    PNSTR
        CALL    CRLF

        LD      A,FUJI          ; READ_HOST_SLOTS: 8 x 32 bytes
        LD      C,0F4h
        LD      B,0
        CALL    FBNEW
        CALL    FBCALL
        LD      A,'3'
        JP      C,FAIL
        JP      NZ,FAIL
        LD      HL,RXBUF+6
        LD      C,'1'
HOSTS:  LD      A,(HL)
        OR      A
        JP      Z,HOST2
        CALL    PRINT
        DEFB    "HOST ",0
        LD      A,C
        CALL    CONOUT
        CALL    PRINT
        DEFB    ": ",0
        LD      B,32
        CALL    PNSTR
        CALL    CRLF
HOST2:  LD      DE,32
        ADD     HL,DE
        INC     C
        LD      A,C
        CP      '9'
        JP      NZ,HOSTS

        LD      HL,URL
        LD      DE,DSPEC
        CALL    COPYZ
        CALL    PRINT
        DEFB    "OPEN:    ",0
        LD      HL,DSPEC
        CALL    PSTR
        CALL    CRLF

        LD      A,NET1          ; OPEN: mode 4 = read, no translation
        LD      C,'O'
        LD      B,2
        CALL    FBNEW
        LD      A,4
        CALL    FBBYTE
        XOR     A
        CALL    FBBYTE
        LD      HL,DSPEC
        CALL    FBSTR
        CALL    FBCALL
        LD      A,'4'
        JP      C,FAIL
        JP      NZ,FAIL

        ; STATUS until bytes are waiting: avail u16, connected, error
        LD      A,20
        LD      (TRIES),A
STAT1:  LD      A,NET1
        LD      C,'S'
        LD      B,0
        CALL    FBNEW
        CALL    FBCALL
        LD      A,'5'
        JP      C,FAIL
        JP      NZ,FAIL
        LD      HL,(RXBUF+6)
        LD      A,H
        OR      L
        JP      NZ,READ1
        LD      A,(TRIES)
        DEC     A
        LD      (TRIES),A
        LD      A,'5'
        JP      Z,FAIL
        JP      STAT1

READ1:  LD      (AVAIL),HL
        LD      A,NET1          ; READ avail bytes: one u16 param
        LD      C,'R'
        LD      B,5
        CALL    FBNEW
        CALL    FBWORD
        CALL    FBCALL
        LD      A,'5'
        JP      C,FAIL
        JP      NZ,FAIL

        ; compare with EXPECT, length and bytes
        LD      A,(EXPLEN)
        CP      C
        LD      A,'6'
        JP      NZ,FAIL
        LD      A,B
        OR      A
        LD      A,'6'
        JP      NZ,FAIL
        LD      DE,EXPECT
CMP1:   LD      A,C
        OR      A
        JP      Z,CMP2
        LD      A,(DE)
        CP      (HL)
        LD      A,'6'
        JP      NZ,FAIL
        INC     HL
        INC     DE
        DEC     C
        JP      CMP1
CMP2:   CALL    PRINT
        DEFB    "READ:    ",0
        LD      HL,EXPECT
        LD      A,(EXPLEN)
        LD      B,A
        CALL    PNSTR

        LD      A,NET1          ; CLOSE
        LD      C,'C'
        LD      B,0
        CALL    FBNEW
        CALL    FBCALL

        CALL    PRINT
        DEFB    "FNTEST PASS",13,0
        JP      EXIT

FAIL:   PUSH    AF
        CALL    PRINT
        DEFB    13,"FNTEST FAIL ",0
        POP     AF
        CALL    CONOUT
        CALL    CRLF
        JP      EXIT

; Copies the NUL-ended string at HL to DE; leaves DE on the NUL.
COPYZ:  LD      A,(HL)
        LD      (DE),A
        OR      A
        RET     Z
        INC     HL
        INC     DE
        JP      COPYZ

URL:    DEFB    "N1:HTTP://127.0.0.1:8046/FNTEST.TXT",0
EXPECT: DEFB    "FUJINET MODEL I FNTEST OK",13,10
EXPLEN: DEFB    EXPLEN-EXPECT
TRIES:  DEFB    0
AVAIL:  DEFW    0

        INCLUDE "m1con.inc"
        INCLUDE "pipe.inc"
        INCLUDE "fnbus.inc"

DSPEC:  DEFS    128
TXBUF:  DEFS    400
RXBUF:  DEFS    768
        DEFS    128
STACK:
