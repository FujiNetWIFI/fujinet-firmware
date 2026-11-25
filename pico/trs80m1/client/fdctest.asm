;=====================================================================
; FDCTEST.ASM -- the card's FD1771 driven directly, on drive 1 (FujiNet
; slot 2) holding mkjv1.py's pattern disk, writable: byte n of sector s
; (counted from the start) is s * 7 + n.
;
; 1 Restore reaches track 0       2 Seek with verify to track 5
; 3 Read Sector 3 matches          4 Read Address gives track 5, 256 bytes
; 5 Write Sector 7, read it back   6 sector 12 is Record Not Found
; 7 track 17 reads with record type FA   8 Force Interrupt ends it all
; Prints FDCTEST PASS or FDCTEST FAIL <step>.
;=====================================================================

        ORG     5200h

FDCCMD  EQU     37ECh
FDCTRK  EQU     37EDh
FDCSEC  EQU     37EEh
FDCDAT  EQU     37EFh
DRVSEL  EQU     37E1h

START:  DI
        LD      SP,STACK
        CALL    CLS
        CALL    PRINT
        DEFB    "FDCTEST: DRIVE 1",13,0

        LD      A,'1'
        LD      (STEP),A
        LD      A,0Bh           ; Restore, head loaded, 20 ms steps
        CALL    CMD
        AND     04h             ; track 0
        JP      Z,FAIL
        LD      A,(FDCTRK)
        OR      A
        JP      NZ,FAIL

        CALL    NEXT
        LD      A,5
        LD      (FDCDAT),A
        LD      A,1Ch           ; Seek, head loaded, verify, 6 ms steps
        CALL    CMD
        AND     18h             ; seek error, CRC
        JP      NZ,FAIL
        LD      A,(FDCTRK)
        CP      5
        JP      NZ,FAIL

        CALL    NEXT
        LD      A,3
        CALL    RDSEC
        AND     1Ch
        JP      NZ,FAIL
        LD      B,53            ; track 5 sector 3
        CALL    CHECK
        JP      NZ,FAIL

        CALL    NEXT
        CALL    SELECT
        LD      A,0C4h          ; Read Address
        LD      (FDCCMD),A
        LD      HL,BUF
        CALL    XIN
        AND     1Ch
        JP      NZ,FAIL
        LD      A,(BUF)
        CP      5
        JP      NZ,FAIL
        LD      A,(BUF+3)
        CP      1               ; 256 bytes
        JP      NZ,FAIL
        LD      A,(FDCSEC)      ; the ID's track lands in the sector register
        CP      5
        JP      NZ,FAIL

        CALL    NEXT
        LD      HL,BUF          ; A5h xor n
        LD      B,0
FILL:   LD      A,L
        XOR     0A5h
        LD      (HL),A
        INC     HL
        DJNZ    FILL
        CALL    SELECT
        LD      A,7
        LD      (FDCSEC),A
        LD      A,0A8h          ; Write Sector
        LD      (FDCCMD),A
        LD      HL,BUF
WR1:    LD      A,(FDCCMD)
        BIT     1,A
        JR      Z,WR2
        LD      A,(HL)
        LD      (FDCDAT),A
        INC     HL
        JR      WR1
WR2:    RRCA
        JR      C,WR1
        RLCA
        AND     5Ch             ; write protect, RNF, CRC, lost data
        JP      NZ,FAIL
        LD      HL,BUF          ; scribble, then read it back over it
        LD      (HL),0
        LD      A,7
        CALL    RDSEC
        AND     1Ch
        JP      NZ,FAIL
        LD      HL,BUF
        LD      B,0
VER1:   LD      A,L
        XOR     0A5h
        CP      (HL)
        JP      NZ,FAIL
        INC     HL
        DJNZ    VER1

        CALL    NEXT
        LD      A,12
        CALL    RDSEC
        AND     10h             ; Record Not Found
        JP      Z,FAIL

        CALL    NEXT
        LD      A,17
        LD      (FDCDAT),A
        LD      A,18h
        CALL    CMD
        XOR     A
        CALL    RDSEC
        AND     7Ch
        CP      20h             ; FA, and nothing wrong
        JP      NZ,FAIL

        CALL    NEXT
        CALL    SELECT
        LD      A,0FEh          ; Write Track, as a doubler probe writes it
        LD      (FDCCMD),A
        LD      A,0D0h          ; and stop it at once
        LD      (FDCCMD),A
        LD      A,(FDCCMD)
        AND     03h             ; neither busy nor asking for data
        JP      NZ,FAIL

        CALL    PRINT
        DEFB    "FDCTEST PASS",13,0
        JP      EXIT

NEXT:   LD      A,(STEP)
        INC     A
        LD      (STEP),A
        RET

FAIL:   CALL    PRINT
        DEFB    13,"FDCTEST FAIL ",0
        LD      A,(STEP)
        CALL    CONOUT
        CALL    PRINT
        DEFB    " STATUS ",0
        LD      A,(FDCCMD)
        CALL    PHEX
        CALL    CRLF
        JP      EXIT

SELECT: PUSH    AF
        LD      A,2
        LD      (DRVSEL),A
        POP     AF
        RET

; Command A; waits for BUSY to drop and returns the status.
CMD:    CALL    SELECT
        LD      (FDCCMD),A
CMD1:   CALL    SELECT          ; keep the motor on
        LD      A,(FDCCMD)
        BIT     0,A
        JR      NZ,CMD1
        RET

; Reads sector A of the current track into BUF; returns the status.
RDSEC:  LD      (FDCSEC),A
        CALL    SELECT
        LD      A,88h
        LD      (FDCCMD),A
        LD      HL,BUF
; Takes bytes into (HL) while DRQ, until BUSY drops; returns the status.
XIN:    LD      A,(FDCCMD)
        BIT     1,A
        JR      Z,XIN1
        LD      A,(FDCDAT)
        LD      (HL),A
        INC     HL
        JR      XIN
XIN1:   BIT     0,A
        JR      NZ,XIN
        RET

; BUF against the pattern of sector B: Z when it matches.
CHECK:  LD      HL,BUF
        LD      A,B             ; s * 7
        ADD     A,A
        ADD     A,A
        ADD     A,A
        SUB     B
        LD      C,A
        LD      B,0
CHK1:   LD      A,C
        CP      (HL)
        RET     NZ
        INC     C
        INC     HL
        DJNZ    CHK1
        XOR     A               ; Z: every byte matched
        RET

STEP:   DEFB    0

        INCLUDE "m1con.inc"

BUF:    DEFS    256
        DEFS    128
STACK:
