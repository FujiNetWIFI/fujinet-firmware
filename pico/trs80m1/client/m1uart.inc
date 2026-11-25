;=====================================================================
; M1UART.INC -- the RS-232 board at E8h-EBh, which the card answers as
; the FujiNet modem.
;
;   UINIT   reset the UART: 9600 baud both ways, 8 bits, no parity
;   MPUT    send A          MGET  A from the modem, wait DE ms (carry set
;                                 on a timeout; the pipe's TICK times it)
;=====================================================================

URESET  EQU     0E8h            ; write: reset
UBAUD   EQU     0E9h            ; write: TX rate high nibble, RX low
USTAT   EQU     0EAh            ; read: DR 80h, THRE 40h; write: control
UDATA   EQU     0EBh

UINIT:  OUT     (URESET),A
        LD      A,0EEh          ; 9600 both ways
        OUT     (UBAUD),A
        LD      A,6Ch           ; 8 bits, one stop bit, no parity, no break
        OUT     (USTAT),A
        RET

MPUT:   PUSH    AF
MPU1:   IN      A,(USTAT)
        AND     40h
        JR      Z,MPU1
        POP     AF
        OUT     (UDATA),A
        RET

MGET:   PUSH    BC
        CALL    PTICK
        LD      B,A
MGE1:   IN      A,(USTAT)
        AND     80h
        JR      NZ,MGE3
        CALL    PTICK
        LD      C,A
        SUB     B
        JR      Z,MGE1
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
        JR      NC,MGE1
        POP     BC
        SCF
        RET
MGE3:   IN      A,(UDATA)
        POP     BC
        OR      A
        RET
