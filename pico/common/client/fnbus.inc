;=====================================================================
; FNBUS.INC -- FujiBus (FEP-004) requests over the pipe, 8080 only.
;
; A packet is {device, command, u16 length, checksum, descriptor} then
; params and payload, sent SLIP-framed. Replies are ACK (06h) or NAK
; (15h) with an optional payload.
;
;   FBNEW   start a packet: A = device, C = command, B = descriptor
;   FBBYTE  append A                FBWORD  append HL, low byte first
;   FBMEM   append BC bytes at HL   FBSTR   append the NUL-ended string at HL
;   FBCALL  send it and wait for the reply. Carry set: no good reply.
;           Carry clear: Z on ACK with HL = payload, BC = its length;
;           NZ on NAK.
; Descriptor bytes: 0 none, 1-4 that many u8 params, 5 one u16.
;=====================================================================

SLEND   EQU     0C0h
SLESC   EQU     0DBh
SLEEND  EQU     0DCh
SLEESC  EQU     0DDh
ACK     EQU     06h
FBTRIES EQU     3
FBWAIT  EQU     3000            ; ms for each reply byte
RXMAX   EQU     700

FBNEW:  PUSH    HL
        LD      HL,TXBUF
        LD      (HL),A
        INC     HL
        LD      (HL),C
        INC     HL
        XOR     A
        LD      (HL),A
        INC     HL
        LD      (HL),A
        INC     HL
        LD      (HL),A
        INC     HL
        LD      (HL),B
        INC     HL
        LD      (TXPTR),HL
        POP     HL
        RET

FBBYTE: PUSH    HL
        LD      HL,(TXPTR)
        LD      (HL),A
        INC     HL
        LD      (TXPTR),HL
        POP     HL
        RET

FBWORD: LD      A,L
        CALL    FBBYTE
        LD      A,H
        JP      FBBYTE

FBMEM:  LD      A,B
        OR      C
        RET     Z
        LD      A,(HL)
        CALL    FBBYTE
        INC     HL
        DEC     BC
        JP      FBMEM

FBSTR:  LD      A,(HL)
        OR      A
        RET     Z
        CALL    FBBYTE
        INC     HL
        JP      FBSTR

; 8-bit sum with end-around carry over BC bytes at HL; result in A.
FBSUM:  PUSH    DE
        LD      D,0
FBSU1:  LD      A,B
        OR      C
        JP      Z,FBSU2
        LD      A,D
        ADD     A,(HL)
        ADC     A,0             ; fold the carry back in
        LD      D,A
        INC     HL
        DEC     BC
        JP      FBSU1
FBSU2:  LD      A,D
        POP     DE
        RET

; Fills in length and checksum, then sends TXBUF SLIP-framed.
FBSEND: LD      HL,(TXPTR)
        LD      A,L
        SUB     TXBUF & 0FFh
        LD      C,A
        LD      A,H
        SBC     A,TXBUF / 256
        LD      B,A             ; BC = length
        LD      HL,TXBUF+2
        LD      (HL),C
        INC     HL
        LD      (HL),B
        INC     HL
        LD      (HL),0
        PUSH    BC
        LD      HL,TXBUF
        CALL    FBSUM
        LD      (TXBUF+4),A
        POP     BC
        LD      HL,TXBUF
        LD      A,SLEND
        CALL    PPUT
FBSE1:  LD      A,B
        OR      C
        JP      Z,FBSE4
        LD      A,(HL)
        CP      SLEND
        JP      NZ,FBSE2
        LD      A,SLESC
        CALL    PPUT
        LD      A,SLEEND
        JP      FBSE3
FBSE2:  CP      SLESC
        JP      NZ,FBSE3
        CALL    PPUT
        LD      A,SLEESC
FBSE3:  CALL    PPUT
        INC     HL
        DEC     BC
        JP      FBSE1
FBSE4:  LD      A,SLEND
        JP      PPUT

; Collects one SLIP frame into RXBUF; BC = its unescaped length. Carry
; set on a timeout or an overlong frame.
FBRECV: LD      DE,FBWAIT
        CALL    PGET
        RET     C
        CP      SLEND
        JP      NZ,FBRECV       ; noise before the frame
        LD      HL,RXBUF
        LD      BC,0
FBRE1:  LD      DE,FBWAIT
        CALL    PGET
        RET     C
        CP      SLEND
        JP      NZ,FBRE2
        LD      A,B
        OR      C
        JP      Z,FBRE1         ; back-to-back ENDs
        OR      A               ; carry clear
        RET
FBRE2:  CP      SLESC
        JP      NZ,FBRE3
        LD      DE,FBWAIT
        CALL    PGET
        RET     C
        CP      SLEEND
        LD      A,SLEND
        JP      Z,FBRE3
        LD      A,SLESC
FBRE3:  LD      (HL),A
        INC     HL
        INC     BC
        LD      A,B
        CP      RXMAX / 256 + 1
        JP      C,FBRE1
        SCF
        RET

; Checks the reply in RXBUF (BC long): length field and checksum, and
; that it comes from the device we asked. Carry set if not.
FBCHK:  LD      A,C
        CP      6
        LD      A,B
        SBC     A,0
        RET     C               ; shorter than a header
        LD      HL,(RXBUF+2)
        LD      A,L
        CP      C
        JP      NZ,FBBAD
        LD      A,H
        CP      B
        JP      NZ,FBBAD
        LD      A,(TXBUF)
        LD      HL,RXBUF
        CP      (HL)
        JP      NZ,FBBAD
        LD      A,(RXBUF+4)
        LD      D,A
        XOR     A
        LD      (RXBUF+4),A
        PUSH    BC
        LD      HL,RXBUF
        CALL    FBSUM
        POP     BC
        CP      D
        JP      NZ,FBBAD
        OR      A
        RET
FBBAD:  SCF
        RET

FBCALL: LD      A,FBTRIES
        LD      (FBTRY),A
FBCA1:  CALL    FBSEND
        CALL    FBRECV
        CALL    NC,FBCHK
        JP      NC,FBCA2
        CALL    PFLUSH          ; resynchronize, then ask again
        LD      A,(FBTRY)
        DEC     A
        LD      (FBTRY),A
        JP      NZ,FBCA1
        SCF
        RET
; Payload: past the header and any params the reply carries (ours carry none).
FBCA2:  LD      HL,-6
        ADD     HL,BC
        LD      B,H
        LD      C,L
        LD      HL,RXBUF+6
        LD      A,(RXBUF+1)
        CP      ACK
        RET                     ; carry clear from CP when A >= ACK

TXPTR:  DEFW    0
FBTRY:  DEFB    0
