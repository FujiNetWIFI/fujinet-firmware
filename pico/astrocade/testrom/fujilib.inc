; fujilib.inc -- Z80 client half of the FujiNet cartridge mailbox.
;
; Hand-mirrors firmware/include/fuji_mailbox.h (the single source of truth);
; every equate here is a console address, i.e. cart offset + 2000H. Keep the
; two files in step.
;
; The cartridge port has no write or I/O visibility, so BOTH directions ride
; reads: a "register write" is a read of FNREGSEL+reg followed by a read of
; FNREGDAT+value (the low address byte IS the payload), and replies are
; plain ROM bytes the cart repaints. A REGDATA read with no immediately
; preceding REGSEL read is ignored by the cart -- that pairing rule is the
; stray-read defense, so these helpers always issue the pair back to back.
;
; Clients run with DI and poll. With interrupts off the I register stays 0,
; refresh cycles land in OS ROM space, and the refresh-stray hazard described
; in fuji_mailbox.h never arises at all.

; ---- reply bytes (repainted ROM) --------------------------------------
FNRDATA EQU     3B00H           ; 256-byte reply slice
FNACKSQ EQU     3C00H           ; echoes SEQ when the reply is ready
FNSTATS EQU     3C01H           ; bit0 link up
FNERR   EQU     3C02H           ; fb_status_t of the last transaction
FNREPLY EQU     3C03H           ; 06H ACK / 15H NAK
FNRXLL  EQU     3C04H           ; reply length, low
FNRXLH  EQU     3C05H           ; reply length, high
FNBSTAT EQU     3C06H           ; boot state
FNBPCT  EQU     3C07H           ; boot progress 0-100
FNBERR  EQU     3C08H           ; boot error
FNMAGF  EQU     3C09H           ; 'F'
FNMAGN  EQU     3C0AH           ; 'N'
FNPVER  EQU     3C0BH           ; protocol version
FNSECHO EQU     3C0CH           ; echoes RXSLICE after each repaint

; ---- hotspot pages ----------------------------------------------------
FNREGSEL EQU    3D00H           ; read +reg: arm register
FNREGDAT EQU    3E00H           ; read +val: armed register = val
FNDATA  EQU     3F00H           ; read +val: append val to TX stream
FNSWAP  EQU     3DFEH           ; armed-only: serve the staged image
FNRSELP EQU     3DH             ; page bytes of the above (zmac 1.3 has no
FNRDATP EQU     3EH             ; HIGH operator)
FNDATAP EQU     3FH

; ---- bank switching (protocol v2) -------------------------------------
; A banked app image is the ordinary 8K window plus 4K pages appended; one
; read maps image page 0-111 into 2000H-2FFFH (the high half never moves --
; client code and the mailbox live there; page 1 IS the high half). The
; select is complete before the next read; the byte the select read returns
; is undefined. Do not execute from 2000H-2FFFH while switching away from
; the page the PC is in. On a cart serving a plain 8K image these reads are
; no-ops, so probing is safe.
FNBKSEL EQU     3D80H           ; read +page: map image page into 2000H-2FFFH
FNBKMAX EQU     112             ; pages 0..111; 3DF0H+ stays reserved
                                ; (3DFEH = FNSWAP)

; Selecting a page is one instruction, no helper needed (a macro here would
; be useless anyway: this file is included after its callers, and zmac
; resolves forward EQUs but not forward macros). Constant page n:
;       LD      A,(FNBKSEL+n)
; Runtime page number in A:
;       LD      D,FNRSELP
;       ADD     A,80H
;       LD      E,A
;       LD      A,(DE)

; ---- register numbers -------------------------------------------------
FRDEVIC EQU     00H
FRCMD   EQU     01H
FRNPARM EQU     02H
FRDATRS EQU     05H
FRRXSLC EQU     06H
FRSEQ   EQU     10H
FRBOOTL EQU     11H             ; BOOTLOCK; magic 0B5H arms the swap
FRBSEL1 EQU     12H             ; BOOTSEL pair: 0B5H then 4AH on FRBSEL2
FRBSEL2 EQU     13H

; ---- values -----------------------------------------------------------
FNDFUJI EQU     70H             ; FujiBus device id of the FujiNet itself
FCACFGX EQU     0C4H            ; GET_ADAPTERCONFIG_EXTENDED
FCMHOST EQU     0F9H            ; MOUNT_HOST
FCMIMG  EQU     0F8H            ; MOUNT_IMAGE
FCSDFP  EQU     0E2H            ; SET_DEVICE_FULLPATH
FCRHSL  EQU     0F4H            ; READ_HOST_SLOTS
FCODIR  EQU     0F7H            ; OPEN_DIRECTORY
FCRDIR  EQU     0F6H            ; READ_DIR_ENTRY
FCCDIR  EQU     0F5H            ; CLOSE_DIRECTORY
FCSDPS  EQU     0E4H            ; SET_DIRECTORY_POSITION
FCACK   EQU     06H             ; reply command: ACK
FCNAK   EQU     15H             ; reply command: NAK
FMREAD  EQU     1               ; disk access mode: read
FEOK    EQU     0               ; FNERR values
FENOLNK EQU     1
FETMOUT EQU     2
FBIDLE  EQU     0               ; FNBSTAT values
FBXFER  EQU     1
FBREADY EQU     2
FBFAIL  EQU     80H
FNBLMAG EQU     0B5H            ; BOOTLOCK magic

; ---- routines ---------------------------------------------------------

; Write mailbox register: C = register number, A = value.
; Clobbers A,B,D,E.
FNREGWR:
        LD      B,A             ; hold the value
        LD      D,FNRSELP
        LD      E,C
        LD      A,(DE)          ; arm the register
        LD      D,FNRDATP
        LD      E,B
        LD      A,(DE)          ; deliver the value
        RET

; Append A to the TX stream. Clobbers A,D,E.
FNTXBYT:
        LD      D,FNDATAP
        LD      E,A
        LD      A,(DE)
        RET

; Append a 1-byte parameter with value A. Clobbers A,B,D,E.
FNPARB:
        LD      B,A
        LD      A,1             ; size byte
        CALL    FNTXBYT
        LD      A,B
        JP      FNTXBYT

; Append the 0-terminated string at HL to the TX stream.
; Clobbers A,D,E,HL.
FNTXSTR:
        LD      A,(HL)
        OR      A
        RET     Z
        CALL    FNTXBYT
        INC     HL
        JR      FNTXSTR

; Begin a transaction: A = device, E = command, L = nparam.
; Rewinds the TX pointer; the caller then streams params and payload.
; Clobbers A,B,C,D,E,H,L.
FNBEGIN:
        PUSH    HL
        PUSH    DE
        PUSH    AF
        LD      C,FRDATRS
        XOR     A
        CALL    FNREGWR
        POP     AF
        LD      C,FRDEVIC
        CALL    FNREGWR
        POP     DE
        LD      A,E
        LD      C,FRCMD
        CALL    FNREGWR
        POP     HL
        LD      A,L
        LD      C,FRNPARM
        JP      FNREGWR

; Commit the transaction and wait for the cart to acknowledge.
; B = timeout in ~2 second quanta. The next sequence number is derived
; from the cart's own persisted ACKSEQ + 1 (never a local counter: RESET
; restarts this program but not the cart), wrapping 255 -> 1, 0 reserved.
; Returns: carry set on timeout; else A = FNERR value (0 = ok).
; Clobbers A,B,C,D,E,H,L.
FNCOMMIT:
        LD      A,(FNACKSQ)
        INC     A
        JR      NZ,FNCMS
        INC     A               ; 255 wraps past the reserved 0
FNCMS:  LD      H,A             ; expected ACKSEQ
        LD      C,FRSEQ
        CALL    FNREGWR
FNCMO:  LD      DE,0            ; inner: 65536 polls, ~2 s at 1.789 MHz
FNCMI:  LD      A,(FNACKSQ)
        CP      H
        JR      Z,FNCMOK
        DEC     DE
        LD      A,D
        OR      E
        JR      NZ,FNCMI
        DJNZ    FNCMO
        SCF                     ; timed out
        RET
FNCMOK: LD      A,(FNERR)
        OR      A               ; clears carry
        RET

; Select reply slice A (0-3) and wait for the repaint to complete: the cart
; publishes FNSECHO last, so the slice is whole once it echoes.
; Returns carry set on timeout. Clobbers A,B,C,D,E,H.
FNSLICE:
        LD      H,A
        LD      C,FRRXSLC
        CALL    FNREGWR
        LD      B,1             ; ~2 s; a live cart repaints in microseconds
FNSLO:  LD      DE,0
FNSLI:  LD      A,(FNSECHO)
        CP      H
        JR      Z,FNSLOK
        DEC     DE
        LD      A,D
        OR      E
        JR      NZ,FNSLI
        DJNZ    FNSLO
        SCF
        RET
FNSLOK: OR      A
        RET

; Is a FujiNet cartridge present? Z set if the magic is there.
; Clobbers A,B.
FNCHECK:
        LD      A,(FNMAGF)
        CP      'F'
        RET     NZ
        LD      A,(FNMAGN)
        CP      'N'
        RET
