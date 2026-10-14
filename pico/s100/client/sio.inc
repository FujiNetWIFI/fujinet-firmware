;=====================================================================
; SIO.INC -- the card's 88-2SIO-compatible pair, 8080 only. Channel A
; (base, base+1) is the FujiNet modem, channel B (base+2, base+3) the
; printer. The base is DIP-switched (14h by default) and patched in by
; SINIT, as FNPORT.INC does for the pipe.
;
;   SINIT   A = base: patch the ports, master-reset both 6850s
;   MPUT    send A to the modem         MGET  A from the modem, wait DE ms
;   LPUT    send A to the printer       (carry set on a timeout)
;=====================================================================

SIOBASE EQU     14h

SINIT:  LD      (MST1+1),A
        LD      (MST2+1),A
        LD      (MCTL1+1),A
        LD      (MCTL2+1),A
        INC     A
        LD      (MDAT1+1),A
        LD      (MDAT2+1),A
        INC     A
        LD      (LST1+1),A
        LD      (LCTL1+1),A
        LD      (LCTL2+1),A
        INC     A
        LD      (LDAT+1),A
        LD      A,03h           ; master reset, then 8N1 /16
MCTL1:  OUT     (0FFh),A
LCTL1:  OUT     (0FFh),A
        LD      A,15h
MCTL2:  OUT     (0FFh),A
LCTL2:  OUT     (0FFh),A
        RET

MPUT:   PUSH    AF
MPU1:
MST1:   IN      A,(0FFh)
        AND     02h
        JP      Z,MPU1
        POP     AF
MDAT1:  OUT     (0FFh),A
        RET

LPUT:   PUSH    AF
LPU1:
LST1:   IN      A,(0FFh)
        AND     02h
        JP      Z,LPU1
        POP     AF
LDAT:   OUT     (0FFh),A
        RET

MGET:   PUSH    BC
        CALL    PTICK
        LD      B,A
MGE1:
MST2:   IN      A,(0FFh)
        AND     01h
        JP      NZ,MGE3
        CALL    PTICK
        LD      C,A
        SUB     B
        JP      Z,MGE1
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
        JP      NC,MGE1
        POP     BC
        SCF
        RET
MGE3:
MDAT2:  IN      A,(0FFh)
        POP     BC
        OR      A
        RET
