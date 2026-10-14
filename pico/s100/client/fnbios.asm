;=====================================================================
; FNBIOS.ASM -- puts FujiNet devices under CP/M 2.2 on the S-100 card.
; CP/M, 8080.
;
;   FNBIOS [E5] [F6] ... [N=TCP://host:port/]
;
; LST: prints on the FujiNet printer. Each Dn names a FujiNet slot n for
; CP/M drive D (E to H): an 8 MB raw image of 512-byte sectors. N= opens
; N1: on a URL for PUN: and RDR:. The resident part (FNBRES.ASM) lives at
; E800h, above a 56K CP/M's BIOS, and the BIOS jump table points at it; a
; warm boot reloads only the CCP and BDOS, so it stays.
;=====================================================================

        ORG     0100h

RES     EQU     0E800h
RPINIT  EQU     RES+36
RFLUSHL EQU     RES+39
ROLD    EQU     RES+42
RSLOTS  EQU     RES+66
RNETOK  EQU     RES+70
NET1    EQU     71h

START:  LD      SP,STACK
        CALL    PRINT
        DEFB    "FNBIOS",13,10,0

        LD      HL,(0001h)      ; the BIOS must sit below us
        LD      A,H
        CP      RES / 256
        JP      NC,TOOHI
        LD      HL,RES          ; and there must be RAM there
        LD      A,(HL)
        CPL
        LD      (HL),A
        CP      (HL)
        JP      NZ,NORAM
        CPL
        LD      (HL),A

        LD      HL,RESIMG       ; copy the resident part into place
        LD      DE,RES
        LD      BC,RESLEN
COPY:   LD      A,(HL)
        LD      (DE),A
        INC     HL
        INC     DE
        DEC     BC
        LD      A,B
        OR      C
        JP      NZ,COPY

        CALL    RPINIT          ; find the card from the resident's own code
        JP      C,NOCARD
        CALL    PINIT           ; and from ours, for N1:'s open below
        CALL    PFLUSH

        LD      HL,0081h        ; options: Dn and N=url
OPT1:   LD      A,(HL)
        OR      A
        JP      Z,OPTX
        INC     HL
        CP      ' '
        JP      Z,OPT1
        CP      'N'
        JP      Z,OPTN
        SUB     'E'             ; E..H
        JP      C,BADOPT
        CP      4
        JP      NC,BADOPT
        LD      C,A
        LD      A,(HL)
        SUB     '1'             ; slot 1..8
        JP      C,BADOPT
        CP      8
        JP      NC,BADOPT
        INC     A
        INC     HL
        PUSH    HL
        LD      B,0
        LD      HL,RSLOTS
        ADD     HL,BC
        LD      (HL),A
        POP     HL
        JP      OPT1
OPTN:   LD      A,(HL)
        CP      '='
        JP      NZ,BADOPT
        INC     HL
        LD      DE,URL          ; N1:<url>
        PUSH    HL
        LD      HL,URLP
        CALL    COPYZ
        POP     HL
OPTN1:  LD      A,(HL)
        OR      A
        JP      Z,OPTN2
        CP      ' '
        JP      Z,OPTN2
        LD      (DE),A
        INC     HL
        INC     DE
        JP      OPTN1
OPTN2:  XOR     A
        LD      (DE),A
        PUSH    HL
        CALL    NETOPEN
        POP     HL
        JP      C,NONET
        LD      A,1
        LD      (RNETOK),A
        JP      OPT1

OPTX:   LD      HL,(0001h)      ; point the BIOS jump table at the resident
        LD      DE,-3
        ADD     HL,DE           ; BIOS base
        EX      DE,HL
        LD      HL,TABLE
        LD      B,12
HOOK:   PUSH    BC
        LD      A,(HL)          ; BIOS entry offset
        INC     HL
        PUSH    HL
        LD      L,A
        LD      H,0
        ADD     HL,DE           ; HL = the JP in the BIOS table
        INC     HL
        PUSH    HL              ; save its old target in the resident
        LD      C,(HL)
        INC     HL
        LD      B,(HL)
        LD      HL,(OLDPTR)
        LD      (HL),C
        INC     HL
        LD      (HL),B
        INC     HL
        LD      (OLDPTR),HL
        POP     HL              ; and point it at our vector
        PUSH    DE
        PUSH    HL
        LD      HL,(VECPTR)
        EX      DE,HL
        POP     HL
        LD      (HL),E
        INC     HL
        LD      (HL),D
        LD      HL,3
        ADD     HL,DE
        LD      (VECPTR),HL
        POP     DE
        POP     HL
        POP     BC
        DEC     B
        JP      NZ,HOOK

        CALL    PRINT
        DEFB    "LST: on the FujiNet printer",13,10,0
        LD      HL,RSLOTS
        LD      C,'E'
SHOW1:  LD      A,(HL)
        OR      A
        JP      Z,SHOW2
        PUSH    AF
        LD      A,C
        CALL    CONOUT
        CALL    PRINT
        DEFB    ": FujiNet slot ",0
        POP     AF
        ADD     A,'0'
        CALL    CONOUT
        CALL    CRLF
SHOW2:  INC     HL
        INC     C
        LD      A,C
        CP      'I'
        JP      NZ,SHOW1
        LD      A,(RNETOK)
        OR      A
        JP      Z,DONE
        CALL    PRINT
        DEFB    "PUN:/RDR: on ",0
        LD      HL,URL
        CALL    PSTR
        CALL    CRLF
DONE:   CALL    PRINT
        DEFB    "FNBIOS installed at E800h",13,10,0
        JP      0               ; warm boot: drives log in afresh

TOOHI:  CALL    PRINT
        DEFB    "FNBIOS: the BIOS is above E800h; needs a 56K CP/M",13,10,0
        JP      0
NORAM:  CALL    PRINT
        DEFB    "FNBIOS: no RAM at E800h",13,10,0
        JP      0
NOCARD: CALL    PRINT
        DEFB    "FNBIOS: no FujiNet card",13,10,0
        JP      0
NONET:  CALL    PRINT
        DEFB    "FNBIOS: can't open ",0
        LD      HL,URL
        CALL    PSTR
        CALL    CRLF
        JP      0
BADOPT: CALL    PRINT
        DEFB    "FNBIOS: options are Dn (D = E to H, n = slot 1 to 8) and N=url",13,10,0
        JP      0

; OPEN N1: read and write, no translation, on URL. Carry set on failure.
NETOPEN:
        LD      A,NET1
        LD      C,'O'
        LD      B,2
        CALL    FBNEW
        LD      A,12            ; read and write
        CALL    FBBYTE
        XOR     A
        CALL    FBBYTE
        LD      HL,URL
        CALL    FBSTR
        CALL    FBCALL
        RET     C
        RET     Z
        SCF
        RET

COPYZ:  LD      A,(HL)
        LD      (DE),A
        OR      A
        RET     Z
        INC     HL
        INC     DE
        JP      COPYZ

; BIOS jump table offsets, in the resident's vector order
TABLE:  DEFB    0Fh,2Dh,12h,15h,1Bh,1Eh,21h,24h,27h,2Ah,18h,30h
OLDPTR: DEFW    ROLD
VECPTR: DEFW    RES
URLP:   DEFB    "N1:",0

        INCLUDE "cpm.inc"
        INCLUDE "fnport.inc"
        INCLUDE "fnbus.inc"

URL:    DEFS    128
TXBUF:  DEFS    400
RXBUF:  DEFS    768
        DEFS    128
STACK:

RESIMG: BINARY  "fnbres.bin"
RESLEN  EQU     $ - RESIMG
