;=====================================================================
; FNPORT.INC -- the FujiNet S-100 card's pipe, 8080 only.
;
; The pipe is four ports at a DIP-switched base: +0 GETC, +1 STATUS,
; +2 PUTC, +3 CONTROL (write) / TICK (read, 1 kHz). The 8080 has no
; IN r,(C), so PINIT finds the base and patches it into every IN/OUT
; below; the code must run from RAM.
;
;   PINIT   find the card: at FNBASE, else scan. Carry set if none.
;   PFLUSH  empty both directions
;   PPUT    send A
;   PGET    receive into A, waiting up to DE ms; carry set on timeout
;   PTICK   the card's millisecond counter into A
;   PWAIT   wait DE ms
;=====================================================================

FNBASE  EQU     0A0h            ; the card's default DIP setting
ST_AVAIL EQU    80h
ST_SIG  EQU     2Ch             ; STATUS bits 6-1 on a card
ST_SIGM EQU     7Eh
ST_TXF  EQU     01h
CTL_FLUSH EQU   08h

; Is there a card at base A? Z set if so. Only STATUS (base+1) is read.
PPROBE: INC     A
        LD      (PPRB1+1),A
PPRB1:  IN      A,(0FFh)
        AND     ST_SIGM
        CP      ST_SIG
        RET

PINIT:  LD      A,FNBASE
        CALL    PPROBE
        LD      A,FNBASE
        JP      Z,PSET
        LD      C,0             ; scan, skipping the FDC and a 2SIO console
PINI1:  LD      A,C
        CP      08h
        JP      Z,PINI2
        CP      10h
        JP      Z,PINI2
        CALL    PPROBE
        LD      A,C
        JP      Z,PSET
PINI2:  LD      A,C
        ADD     A,4
        LD      C,A
        JP      NZ,PINI1
        SCF
        RET

; Patch base A into every port operand.
PSET:   LD      (PBASE),A
        LD      (PGETC+1),A
        INC     A
        LD      (PSTAT1+1),A
        LD      (PSTAT2+1),A
        INC     A
        LD      (PPUTC+1),A
        INC     A
        LD      (PCTL+1),A
        LD      (PTICK1+1),A
        LD      (PTICK2+1),A
        LD      (PTICK3+1),A
        OR      A               ; carry clear
        RET

PBASE:  DEFB    0

PTICK:
PTICK3: IN      A,(0FFh)
        RET

PWAIT:  PUSH    BC
        CALL    PTICK
        LD      B,A
PWAI1:  CALL    PTICK
        LD      C,A
        SUB     B
        JP      Z,PWAI1
        LD      B,C
        PUSH    HL
        LD      L,A
        LD      A,E
        SUB     L
        LD      E,A
        LD      A,D
        SBC     A,0
        LD      D,A
        POP     HL
        JP      C,PWAI2
        OR      E
        JP      NZ,PWAI1
PWAI2:  POP     BC
        RET

PFLUSH: LD      A,CTL_FLUSH
PCTL:   OUT     (0FFh),A
        RET

; TXFULL is advisory: wait a little for room, then send anyway.
PPUT:   PUSH    AF
        PUSH    BC
        LD      B,0
PPUT1:
PSTAT1: IN      A,(0FFh)
        AND     ST_TXF
        JP      Z,PPUT2
        DEC     B
        JP      NZ,PPUT1
PPUT2:  POP     BC
        POP     AF
PPUTC:  OUT     (0FFh),A
        RET

; Waits for a byte, timing out on the card's millisecond TICK, so the
; timeout means the same at any CPU speed.
PGET:   PUSH    BC
PTICK1: IN      A,(0FFh)
        LD      B,A             ; last tick seen
PGET1:
PSTAT2: IN      A,(0FFh)
        AND     ST_AVAIL
        JP      NZ,PGET3
PTICK2: IN      A,(0FFh)
        LD      C,A
        SUB     B               ; ms since last look
        JP      Z,PGET1
        LD      B,C
        PUSH    HL
        LD      L,A
        LD      A,E
        SUB     L
        LD      E,A
        LD      A,D
        SBC     A,0
        LD      D,A
        POP     HL
        JP      NC,PGET1
        POP     BC
        SCF                     ; timed out
        RET
PGET3:
PGETC:  IN      A,(0FFh)
        POP     BC
        OR      A               ; carry clear
        RET
