;=====================================================================
; LOADER.ASM -- boots drive 0 through the card's FDC+ the way the MITS
; DBL and CDBL do, so any Altair disk they boot boots here. 8080 only.
;
; Runs at 4C00h, as CDBL does. Reads track 0 onward, even sectors then
; odd, into memory from 0000h until the file size in the sector header is
; loaded, then turns the boot page off when it shadows RAM and jumps to
; 0000h. A system-track sector is:
;   [0] track+80h  [1-2] file size  [3-130] data  [131] FFh  [132] checksum
; Retries a bad sector 16 times, then prints C (checksum) or M (memory)
; on a 2SIO console and halts, as the DBL does.
;=====================================================================

        ORG     4C00h

DSTAT   EQU     08h             ; IN: status, active low
DSLCT   EQU     08h             ; OUT: select
DCTRL   EQU     09h             ; OUT: command
DSECT   EQU     09h             ; IN: sector position
DDATA   EQU     0Ah
ENABLD  EQU     08h
MOVEOK  EQU     02h
TRACK0  EQU     40h
NRDA    EQU     80h
STEPIN  EQU     01h
STEPOUT EQU     02h
HDLOAD  EQU     04h
RETRIES EQU     16

        JP      START

; Card info: the RP fills in the bytes after the magic as it streams us.
INFO:   DEFB    "FNS1"
IBASE:  DEFB    0               ; pipe base
IPAGE:  DEFB    0
ICON:   DEFB    0
IFLAGS: DEFB    0               ; bit 0: the page uses PHANTOM*
ISPT:   DEFB    0               ; drive 0 sectors per track, 0 if unknown

START:  LD      SP,STACK
        XOR     A               ; drive 0, and wait until it is ready
WAITEN: OUT     (DSLCT),A
        IN      A,(DSTAT)
        AND     ENABLD
        LD      A,0
        JP      NZ,WAITEN
        LD      A,HDLOAD
        OUT     (DCTRL),A
HOME:   IN      A,(DSTAT)       ; step out to track 0
        AND     TRACK0
        JP      Z,HOMED
        CALL    MOVWT
        LD      A,STEPOUT
        OUT     (DCTRL),A
        JP      HOME
HOMED:  LD      A,(ISPT)        ; 16 for a minidisk, else 32
        CP      16
        JP      Z,SETSPT
        LD      A,32
SETSPT: LD      (SPT),A
        LD      HL,0
        LD      (DMA),HL
        LD      (SIZE),HL
        LD      A,0FFh
        LD      (FIRST),A
        XOR     A
        LD      (TRACK),A
TRKLP:  XOR     A               ; evens first
        LD      (SECT),A
SECLP:  LD      A,RETRIES
        LD      (TRIES),A
RETRY:  CALL    RDSEC
        JP      NC,GOOD
        LD      A,(TRIES)
        DEC     A
        LD      (TRIES),A
        JP      NZ,RETRY
        LD      A,'C'
        JP      ABORT
GOOD:   CALL    STORE           ; Z set when the file is all loaded
        JP      Z,DONE
        LD      A,(SPT)
        LD      B,A
        LD      A,(SECT)
        ADD     A,2
        LD      (SECT),A
        CP      B
        JP      C,SECLP
        AND     1               ; past the end: evens done means odds next
        JP      NZ,NXTTRK
        LD      A,1
        LD      (SECT),A
        JP      SECLP
NXTTRK: CALL    MOVWT
        LD      A,STEPIN
        OUT     (DCTRL),A
        LD      A,(TRACK)
        INC     A
        LD      (TRACK),A
        JP      TRKLP

DONE:   LD      A,80h           ; deselect, as the DBL does
        OUT     (DSLCT),A
        LD      A,(IFLAGS)
        AND     01h
        JP      Z,GO
        LD      A,(IBASE)       ; PHANTOM*: give the page's RAM back
        ADD     A,3
        LD      (ROMOFF+1),A
        LD      A,10h
ROMOFF: OUT     (0FFh),A
GO:     JP      0000h

; Wait for the head to be movable.
MOVWT:  IN      A,(DSTAT)
        AND     MOVEOK
        JP      NZ,MOVWT
        RET

; Read sector SECT of the current track into BUF. Carry set on a bad sector.
RDSEC:  LD      A,(SECT)
        LD      B,A
RDS1:   IN      A,(DSECT)
        RRA                     ; sector true is bit 0, active low
        JP      C,RDS1
        AND     1Fh
        CP      B
        JP      NZ,RDS1
        LD      HL,BUF
        LD      C,137
RDS2:   IN      A,(DSTAT)
        OR      A
        JP      M,RDS2          ; wait for NRDA
        IN      A,(DDATA)
        LD      (HL),A
        INC     HL
        DEC     C
        JP      NZ,RDS2
        LD      A,(BUF+131)     ; sync byte
        CP      0FFh
        SCF
        RET     NZ
        LD      HL,BUF+3        ; checksum of the 128 data bytes
        LD      B,128
        XOR     A
RDS3:   ADD     A,(HL)
        INC     HL
        DEC     B
        JP      NZ,RDS3
        LD      HL,BUF+132
        CP      (HL)
        SCF
        RET     NZ
        OR      A
        RET

; Copy BUF's data to DMA and verify it landed; Z set once SIZE bytes are in.
STORE:  LD      A,(FIRST)
        OR      A
        JP      Z,STO1
        LD      HL,(BUF+1)      ; the file size, from the first sector
        LD      (SIZE),HL
        XOR     A
        LD      (FIRST),A
STO1:   LD      HL,BUF+3
        EX      DE,HL
        LD      HL,(DMA)
        LD      B,128
STO2:   LD      A,(DE)
        LD      (HL),A
        CP      (HL)
        LD      A,'M'
        JP      NZ,ABORT
        INC     HL
        INC     DE
        DEC     B
        JP      NZ,STO2
        LD      (DMA),HL
        LD      A,(SIZE)        ; done when DMA >= SIZE
        LD      E,A
        LD      A,(SIZE+1)
        LD      D,A
        LD      A,L
        SUB     E
        LD      A,H
        SBC     A,D
        JP      C,STO3
        XOR     A               ; Z: done
        RET
STO3:   OR      1               ; NZ: more to load
        RET

; Print the error letter on a 2SIO at 10h/11h and stop, as the DBL does.
ABORT:  LD      B,A
ABO1:   IN      A,(10h)
        AND     02h
        JP      Z,ABO1
        LD      A,B
        OUT     (11h),A
        HALT
        JP      ABORT

SPT:    DEFB    0
SECT:   DEFB    0
TRACK:  DEFB    0
TRIES:  DEFB    0
FIRST:  DEFB    0
DMA:    DEFW    0
SIZE:   DEFW    0
BUF:    DEFS    137
        DEFS    32
STACK:
