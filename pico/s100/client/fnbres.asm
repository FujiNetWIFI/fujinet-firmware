;=====================================================================
; FNBRES.ASM -- FNBIOS's resident part, assembled to run at E800h.
; FNBIOS.ASM copies it there and points the BIOS jump table at it.
;
;   LST:          lines to the FujiNet printer (PRINTER_WRITE)
;   PUN:, RDR:    N1:, opened by the installer on a URL
;   E: to H:      FujiNet slots holding 8 MB raw images of 512-byte
;                 sectors, as CP/M disks of 4 KB blocks and 1024
;                 directory entries; reads cache one sector, writes go
;                 straight through
; Everything else goes on to the BIOS that was there. 8080 only.
;=====================================================================

        ORG     0E800h

NET1    EQU     71h
PRINTER EQU     40h
DISK    EQU     30h             ; slot n is device 30h+n
NDRV    EQU     4               ; E: to H:
FIRST   EQU     4               ; E: is CP/M drive 4
SECSZ   EQU     512

; Fixed layout (FNBIOS.ASM depends on it): twelve BIOS entries and two
; for the installer, then the BIOS entries we chain to, then the setup.
        JP      LIST            ; +0
        JP      LISTST
        JP      PUNCH
        JP      READER
        JP      SELDSK
        JP      SETTRK
        JP      SETSEC
        JP      SETDMA
        JP      READ
        JP      WRITE
        JP      HOME
        JP      SECTRAN         ; +33
        JP      PINIT           ; +36: find the card, patch our ports
        JP      FLUSHL          ; +39
OLIST:  DEFW    0               ; +42, in the order above
OLISTS: DEFW    0
OPUN:   DEFW    0
ORDR:   DEFW    0
OSELD:  DEFW    0
OTRK:   DEFW    0
OSEC:   DEFW    0
ODMA:   DEFW    0
OREAD:  DEFW    0
OWRITE: DEFW    0
OHOME:  DEFW    0
OXLT:   DEFW    0
SLOTS:  DEFB    0,0,0,0         ; +66: FujiNet slot for E:-H:, 0 = not ours
NETOK:  DEFB    0               ; +70: N1: is open for PUN:/RDR:

; ---- LST: ----
LIST:   PUSH    HL
        LD      HL,(LSTPTR)
        LD      (HL),C
        INC     HL
        LD      (LSTPTR),HL
        LD      A,C
        CP      10
        JP      Z,LIS1
        CP      12
        JP      Z,LIS1
        LD      A,L
        CP      (LSTBUF+128) & 0FFh
        JP      NZ,LIS2
LIS1:   CALL    FLUSHL
LIS2:   POP     HL
        RET

LISTST: LD      A,0FFh
        RET

FLUSHL: PUSH    BC
        PUSH    DE
        PUSH    HL
        LD      HL,(LSTPTR)
        LD      A,L
        SUB     LSTBUF & 0FFh
        LD      C,A
        LD      B,0
        JP      Z,FLL1
        LD      A,PRINTER
        PUSH    BC
        LD      C,'W'
        LD      B,0
        CALL    FBNEW
        POP     BC
        LD      HL,LSTBUF
        CALL    FBMEM
        CALL    FBCALL
        LD      HL,LSTBUF
        LD      (LSTPTR),HL
FLL1:   POP     HL
        POP     DE
        POP     BC
        RET

; ---- PUN: and RDR: on N1: ----
PUNCH:  LD      A,(NETOK)
        OR      A
        RET     Z
        PUSH    HL
        LD      HL,(PUNPTR)
        LD      (HL),C
        INC     HL
        LD      (PUNPTR),HL
        LD      A,C
        CP      10
        JP      Z,PUN1
        CP      1Ah
        JP      Z,PUN1
        LD      A,L
        CP      (PUNBUF+64) & 0FFh
        JP      NZ,PUN2
PUN1:   CALL    FLUSHP
PUN2:   POP     HL
        RET

FLUSHP: PUSH    BC
        PUSH    DE
        PUSH    HL
        LD      HL,(PUNPTR)
        LD      A,L
        SUB     PUNBUF & 0FFh
        LD      C,A
        LD      B,0
        JP      Z,FLP1
        PUSH    BC
        LD      L,C             ; WRITE: one u16, the length
        LD      H,0
        LD      A,NET1
        LD      C,'W'
        LD      B,5
        CALL    FBNEW
        CALL    FBWORD
        POP     BC
        LD      HL,PUNBUF
        CALL    FBMEM
        CALL    FBCALL
        LD      HL,PUNBUF
        LD      (PUNPTR),HL
FLP1:   POP     HL
        POP     DE
        POP     BC
        RET

; Next byte from N1:, waiting for it; ^Z once the far end has nothing more.
READER: LD      A,(NETOK)
        OR      A
        LD      A,1Ah
        RET     Z
        PUSH    BC
        PUSH    DE
        PUSH    HL
RDR1:   LD      HL,(RDRPTR)
        LD      A,(RDREND)
        CP      L
        JP      NZ,RDR3
        CALL    FLUSHP          ; anything still going out goes first
        LD      A,NET1          ; STATUS: avail u16, connected, error
        LD      C,'S'
        LD      B,0
        CALL    FBNEW
        CALL    FBCALL
        JP      C,RDREOF
        LD      HL,(RXBUF+6)
        LD      A,H
        OR      L
        JP      NZ,RDR2
        LD      A,(RXBUF+8)     ; nothing waiting: gone, or just slow?
        OR      A
        JP      Z,RDREOF
        JP      RDR1
RDR2:   LD      A,H             ; at most 64 at a time
        OR      A
        JP      NZ,RDR2A
        LD      A,L
        CP      64
        JP      C,RDR2B
RDR2A:  LD      HL,64
RDR2B:  LD      A,NET1          ; READ: one u16
        LD      C,'R'
        LD      B,5
        CALL    FBNEW
        CALL    FBWORD
        CALL    FBCALL
        JP      C,RDREOF
        LD      A,C             ; copy the payload to RDRBUF
        LD      DE,RDRBUF
RDR2C:  OR      A
        JP      Z,RDR2D
        LD      B,A
        LD      A,(HL)
        LD      (DE),A
        INC     HL
        INC     DE
        LD      A,B
        DEC     A
        JP      RDR2C
RDR2D:  LD      A,E
        LD      (RDREND),A
        LD      HL,RDRBUF
        LD      (RDRPTR),HL
        JP      RDR1
RDR3:   LD      A,(HL)
        INC     HL
        LD      (RDRPTR),HL
        JP      RDRX
RDREOF: LD      A,1Ah
RDRX:   POP     HL
        POP     DE
        POP     BC
        RET

; ---- E: to H: ----
; Which of our drives is selected: its slot, 0 when the BIOS has it.
SELDSK: LD      A,C
        SUB     FIRST
        JP      C,SEL1
        CP      NDRV
        JP      NC,SEL1
        LD      E,A             ; ours if it has a slot
        LD      D,0
        LD      HL,SLOTS
        ADD     HL,DE
        LD      A,(HL)
        OR      A
        JP      Z,SEL1
        LD      (CURSLT),A
        LD      L,E             ; HL = DPH + 16 * n
        LD      H,0
        ADD     HL,HL
        ADD     HL,HL
        ADD     HL,HL
        ADD     HL,HL
        LD      DE,DPH
        ADD     HL,DE
        RET
SEL1:   XOR     A
        LD      (CURSLT),A
        LD      HL,(OSELD)
        JP      (HL)

; Track, sector and DMA: kept for us, and passed on.
SETTRK: LD      H,B
        LD      L,C
        LD      (TRK),HL
        LD      HL,(OTRK)
        JP      (HL)
SETSEC: LD      H,B
        LD      L,C
        LD      (SEC),HL
        LD      HL,(OSEC)
        JP      (HL)
SETDMA: LD      H,B
        LD      L,C
        LD      (DMA),HL
        LD      HL,(ODMA)
        JP      (HL)
HOME:   LD      HL,0
        LD      (TRK),HL
        LD      HL,(OHOME)
        JP      (HL)
SECTRAN:
        LD      A,(CURSLT)
        OR      A
        JP      Z,SXL1
        LD      H,B             ; no skew on ours
        LD      L,C
        RET
SXL1:   LD      HL,(OXLT)
        JP      (HL)

READ:   LD      A,(CURSLT)
        OR      A
        JP      NZ,RD1
        LD      HL,(OREAD)
        JP      (HL)
RD1:    CALL    FETCH           ; the 512-byte sector this record is in
        LD      A,1
        RET     C
        CALL    RECPTR          ; HL = the record in the cache
        EX      DE,HL
        LD      HL,(DMA)
        EX      DE,HL
        CALL    MOV128          ; cache -> DMA
        XOR     A
        RET

WRITE:  LD      A,(CURSLT)
        OR      A
        JP      NZ,WR1
        LD      HL,(OWRITE)
        JP      (HL)
WR1:    CALL    FETCH           ; read, patch, write back the whole sector
        LD      A,1
        RET     C
        CALL    RECPTR
        EX      DE,HL           ; DE = record in the cache
        LD      HL,(DMA)
        CALL    MOV128          ; DMA -> cache
        LD      A,(CURSLT)      ; WRITE: u32 sector, 512 bytes
        ADD     A,DISK
        LD      C,'W'
        LD      B,7
        CALL    FBNEW
        LD      HL,(CLBA)
        CALL    FBWORD
        LD      HL,0
        CALL    FBWORD
        LD      HL,CACHE
        LD      BC,SECSZ
        CALL    FBMEM
        CALL    FBCALL
        LD      A,1
        RET     C
        RET     NZ              ; NAK: A is still 1
        XOR     A
        RET

; LBA of the current record: (track * 64 + sector) / 4 into CLBAW.
LBAOF:  LD      HL,(TRK)
        ADD     HL,HL           ; * 64
        ADD     HL,HL
        ADD     HL,HL
        ADD     HL,HL
        ADD     HL,HL
        ADD     HL,HL
        EX      DE,HL
        LD      HL,(SEC)
        ADD     HL,DE
        LD      A,L             ; record within the sector
        AND     3
        LD      (RECNO),A
        LD      A,H             ; / 4
        OR      A
        RRA
        LD      H,A
        LD      A,L
        RRA
        LD      L,A
        LD      A,H
        OR      A
        RRA
        LD      H,A
        LD      A,L
        RRA
        LD      L,A
        RET

; Make the cache hold the current record's sector. Carry set on failure.
FETCH:  CALL    LBAOF
        LD      A,(CSLOT)
        LD      B,A
        LD      A,(CURSLT)
        CP      B
        JP      NZ,FET1
        LD      A,(CLBA)
        CP      L
        JP      NZ,FET1
        LD      A,(CLBA+1)
        CP      H
        RET     Z               ; carry clear from the equal compare
FET1:   LD      (CLBA),HL
        XOR     A
        LD      (CSLOT),A       ; invalid until the read lands
        LD      A,(CURSLT)
        ADD     A,DISK
        LD      C,'R'
        LD      B,7
        CALL    FBNEW
        LD      HL,(CLBA)
        CALL    FBWORD
        LD      HL,0
        CALL    FBWORD
        CALL    FBCALL
        RET     C
        SCF
        RET     NZ              ; NAK
        LD      A,B             ; a whole sector, or it didn't happen
        CP      SECSZ / 256
        SCF
        RET     NZ
        LD      DE,CACHE
        LD      BC,SECSZ
FET2:   LD      A,(HL)
        LD      (DE),A
        INC     HL
        INC     DE
        DEC     BC
        LD      A,B
        OR      C
        JP      NZ,FET2
        LD      A,(CURSLT)
        LD      (CSLOT),A
        OR      A
        RET

RECPTR: LD      A,(RECNO)
        RRCA                    ; * 128
        LD      L,A
        AND     80h
        LD      L,A
        LD      A,(RECNO)
        RRA
        AND     01h
        LD      H,A
        LD      DE,CACHE
        ADD     HL,DE
        RET

; Copy 128 bytes from HL to DE.
MOV128: LD      B,128
MOV1:   LD      A,(HL)
        LD      (DE),A
        INC     HL
        INC     DE
        DEC     B
        JP      NZ,MOV1
        RET

        INCLUDE "fnport.inc"
        INCLUDE "fnbus.inc"

; A disk of 8 MB: 64 records a track, 1024 tracks, 4 KB blocks, 1024
; directory entries in 8 blocks, nothing reserved, no checksum vector.
DPB:    DEFW    64              ; SPT
        DEFB    5,31,1          ; BSH, BLM, EXM
        DEFW    2047            ; DSM
        DEFW    1023            ; DRM
        DEFB    0FFh,0          ; AL0, AL1
        DEFW    0               ; CKS
        DEFW    0               ; OFF

DPH:    DEFW    0,0,0,0,DIRBUF,DPB,0,ALV0
        DEFW    0,0,0,0,DIRBUF,DPB,0,ALV1
        DEFW    0,0,0,0,DIRBUF,DPB,0,ALV2
        DEFW    0,0,0,0,DIRBUF,DPB,0,ALV3

CURSLT: DEFB    0
TRK:    DEFW    0
SEC:    DEFW    0
DMA:    DEFW    0080h
RECNO:  DEFB    0
CSLOT:  DEFB    0
CLBA:   DEFW    0
LSTPTR: DEFW    LSTBUF
PUNPTR: DEFW    PUNBUF
RDRPTR: DEFW    RDRBUF
RDREND: DEFB    RDRBUF & 0FFh

        ALIGN   256
LSTBUF: DEFS    128
PUNBUF: DEFS    64
RDRBUF: DEFS    64
DIRBUF: DEFS    128
ALV0:   DEFS    256
ALV1:   DEFS    256
ALV2:   DEFS    256
ALV3:   DEFS    256
CACHE:  DEFS    SECSZ
TXBUF:  DEFS    540
RXBUF:  DEFS    768
RESEND:
