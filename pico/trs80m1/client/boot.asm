;=====================================================================
; BOOT.ASM -- the boot sector of the card's disks: CONFIG's and the test
; disks. The Level II ROM loads it from track 0 sector 0 to 4200h and
; jumps here; it reads the program from track 1 on through the FD1771
; and runs it. mkjv1.py fills in the "FNM1" block. Interrupts stay off:
; the ROM's 4012h vector is EI;RET, and INT* is a level.
;=====================================================================

FDCCMD  EQU     37ECh
FDCTRK  EQU     37EDh
FDCSEC  EQU     37EEh
FDCDAT  EQU     37EFh
DRVSEL  EQU     37E1h
SPT     EQU     10
TRIES   EQU     5
PIPEST  EQU     0D1h            ; the card's pipe STATUS
PIPECTL EQU     0D3h            ; and CONTROL
CTLRAM  EQU     41h             ; CONTROL: turn the card's RAM on

        ORG     4200h

BOOT:   DI
        LD      SP,4200h
        JR      START

        DEFM    "FNM1"
LOADAT: DEFW    5200h           ; where the program goes
ENTRY:  DEFW    5200h
COUNT:  DEFW    1               ; its sectors
NEEDS:  DEFB    0               ; 1: it needs RAM at 8000h-FFFFh

START:  LD      A,(NEEDS)
        OR      A
        CALL    NZ,RAMCHK
        LD      HL,(LOADAT)
        LD      DE,(COUNT)
        LD      BC,0100h        ; B track 1, C sector 0
NEXT:   CALL    READ
        INC     C
        LD      A,C
        CP      SPT
        JR      NZ,NEXT1
        LD      C,0
        INC     B
NEXT1:  DEC     DE
        LD      A,D
        OR      E
        JR      NZ,NEXT
        LD      HL,(ENTRY)
        JP      (HL)

; Sector C of track B to (HL); HL moves past it.
READ:   LD      A,TRIES
        LD      (TRY),A
READ0:  PUSH    HL
        LD      A,1
        LD      (DRVSEL),A      ; drive 0, and the motor on
        LD      A,B
        LD      (FDCDAT),A
        LD      A,18h           ; Seek, head loaded, 6 ms steps
        LD      (FDCCMD),A
        CALL    WAIT
        LD      A,C
        LD      (FDCSEC),A
        LD      A,88h           ; Read Sector
        LD      (FDCCMD),A
READ1:  LD      A,(FDCCMD)
        BIT     1,A
        JR      NZ,READ2
        RRCA
        JR      C,READ1         ; still busy
        AND     1Ch >> 1        ; RNF, CRC, lost data
        JR      Z,READ3
        POP     HL
        LD      A,(TRY)
        DEC     A
        LD      (TRY),A
        JR      NZ,READ0
        CALL    SAY
        DEFB    "BOOT: DISK ERROR",0
        JR      $
READ2:  LD      A,(FDCDAT)
        LD      (HL),A
        INC     HL
        JR      READ1
READ3:  POP     AF              ; drop the start; HL is past the sector
        RET

WAIT:   LD      A,1
        LD      (DRVSEL),A
        LD      A,(FDCCMD)
        RRCA
        JR      C,WAIT
        RET

; RAM at 8000h-FFFFh, or ask to turn the card's on.
RAMCHK: LD      HL,0FFFFh
        LD      A,5Ah
        LD      (HL),A
        CP      (HL)
        RET     Z
        CALL    SAY
        DEFB    "NEEDS 32K. R: TURN ON THE CARD'S RAM",0
RAMCH1: LD      A,(3804h)       ; keyboard row 2: P-W, R is bit 2
        AND     04h
        JR      Z,RAMCH1
        IN      A,(PIPEST)
        AND     7Eh
        CP      2Ch
        JR      NZ,RAMCH1       ; no card answering: nothing to turn on
        LD      A,CTLRAM
        OUT     (PIPECTL),A
        JP      0               ; and boot again

; Prints the 0-terminated string after the CALL on line 15.
SAY:    EX      (SP),HL
        LD      DE,3FC0h
SAY1:   LD      A,(HL)
        INC     HL
        OR      A
        JR      Z,SAY2
        LD      (DE),A
        INC     DE
        JR      SAY1
SAY2:   EX      (SP),HL
        RET

TRY:    DEFB    0

        DEFS    BOOT + 256 - $
