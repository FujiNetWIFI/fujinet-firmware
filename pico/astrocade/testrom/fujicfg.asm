; fujicfg.asm -- FujiNet CONFIG for the Bally Astrocade.
;
; Milestone 3: pick a host slot, browse the root of its TNFS tree, pick an
; image, boot it. Navigation is the player 1 hand controller: stick up/down
; moves, left/right pages the file list, trigger selects. RESET on the
; console returns to the host page (the client restarts; the cart keeps its
; state, which is why sequence numbers derive from ACKSEQ -- see
; fujilib.inc).
;
; Kept to the o2 CONFIG's proven scope: root listing only, no subdirectory
; descent yet. Display names are crunched to the row width, so the boot
; path re-reads the selected entry at full length first (READ_DIR_ENTRY
; returns what fits in the maxlen it is given; a crunched name would never
; open).

        INCLUDE "HVGLIB.H"

; Screen RAM: 88 visible lines use 4000H-4DBFH; everything above is ours.
ENTBUF  EQU     4E00H           ; "/" + full entry name, for the boot path
LINBUF  EQU     4F00H           ; display line being built
HEXBUF  EQU     4F30H
V_HOST  EQU     4F40H           ; selected host slot
V_POS   EQU     4F41H           ; directory index of the top row
V_CUR   EQU     4F42H           ; cursor row, 0-7
V_CNT   EQU     4F43H           ; entries on this page, 0-8
STUB    EQU     4FE0H
STACK   EQU     4FC0H

LINES   EQU     88
OPTFB   EQU     0CH
OPTHI   EQU     04H             ; highlighted row: fg color 1
NROWS   EQU     8
NAMELEN EQU     18              ; display width
FULLLEN EQU     120             ; re-read width for the boot path
DEVSLOT EQU     0

; Player 1 handle, port 10H, active high.
BUP     EQU     01H
BDOWN   EQU     02H
BLEFT   EQU     04H
BRIGHT  EQU     08H
BTRIG   EQU     10H

        ORG     FIRSTC
        DB      55H
        DW      MENUST
        DW      PRGNAM
        DW      PRGSTR
PRGNAM: DB      "FUJINET"
        DB      0

PRGSTR: DI
        LD      SP,STACK
        SYSTEM  INTPC
        DO      SETOUT
        DB      LINES*2
        DB      0
        DB      8
        DO      COLSET
        DW      PALET
        DO      FILL
        DW      NORMEM
        DW      LINES*BYTEPL
        DB      0
        DO      STRDIS
        DB      2
        DB      2
        DB      OPTFB
        DW      TSTR
        EXIT

        CALL    FNCHECK
        JP      NZ,NOCARD

; ======================================================================
; Host page
; ======================================================================
HOSTPG: XOR     A
        LD      (V_HOST),A

; READ_HOST_SLOTS: the reply is 8 records of 32 bytes = 256, which is
; exactly slice 0 on this port -- no slice paging needed anywhere here.
        LD      A,FNDFUJI
        LD      E,FCRHSL
        LD      L,0
        CALL    FNBEGIN
        LD      B,8
        CALL    FNCOMMIT
        LD      HL,EHOSTS
        JP      C,FAILT
        JP      NZ,FAILE

HDRAW:  CALL    CLRLST
        SYSSUK  STRDIS
        DB      120
        DB      2
        DB      OPTFB
        DW      THOSTS
        LD      B,0             ; slot
HDROW:  PUSH    BC
        LD      A,B
        RRCA                    ; slot * 32 = reply offset
        RRCA
        RRCA                    ; A = slot << 5 (slot < 8)
        LD      L,A
        LD      H,0
        LD      DE,FNRDATA
        ADD     HL,DE
        LD      A,(HL)
        OR      A
        JR      NZ,HDNAM
        LD      HL,TEMPTY       ; empty slot
HDNAM:  LD      B,NAMELEN
        CALL    CPYSAN
        POP     BC
        PUSH    BC
        LD      A,B             ; row y = 14 + slot*8
        ADD     A,A
        ADD     A,A
        ADD     A,A
        ADD     A,14
        LD      D,A
        LD      A,(V_HOST)
        CP      B
        LD      C,OPTFB
        JR      NZ,HDOPT
        LD      C,OPTHI
HDOPT:  CALL    SHOWROW
        POP     BC
        INC     B
        LD      A,B
        CP      8
        JR      C,HDROW

HKEY:   CALL    GETKEY
        CP      BUP
        JR      NZ,HK1
        LD      A,(V_HOST)
        OR      A
        JR      Z,HKEY
        DEC     A
        LD      (V_HOST),A
        JR      HDRAW
HK1:    CP      BDOWN
        JR      NZ,HK2
        LD      A,(V_HOST)
        CP      7
        JR      NC,HKEY
        INC     A
        LD      (V_HOST),A
        JR      HDRAW
HK2:    CP      BTRIG
        JR      NZ,HKEY

; ---- MOUNT_HOST + OPEN_DIRECTORY("/") --------------------------------
        LD      A,FNDFUJI
        LD      E,FCMHOST
        LD      L,1
        CALL    FNBEGIN
        LD      A,(V_HOST)
        CALL    FNPARB
        LD      B,8
        CALL    FNCOMMIT
        LD      HL,EMHOST
        JP      C,FAILT
        JP      NZ,FAILE
        LD      A,(FNREPLY)
        CP      FCACK
        LD      HL,EMHOST
        JP      NZ,FAILN

; OPEN_DIRECTORY(host): payload is "path\0filter\0" NUL-padded to exactly
; 256 bytes (the ESP32 reads it into a fixed buffer and fails a short one).
        LD      A,FNDFUJI
        LD      E,FCODIR
        LD      L,1
        CALL    FNBEGIN
        LD      A,(V_HOST)
        CALL    FNPARB
        LD      A,'/'
        CALL    FNTXBYT
        LD      B,255           ; 255 zeros complete path+filter padding
ODPAD:  XOR     A
        CALL    FNTXBYT
        DJNZ    ODPAD
        LD      B,8
        CALL    FNCOMMIT
        LD      HL,EODIR
        JP      C,FAILT
        JP      NZ,FAILE
        LD      A,(FNREPLY)
        CP      FCACK
        LD      HL,EODIR
        JP      NZ,FAILN

        XOR     A
        LD      (V_POS),A
        LD      (V_CUR),A

; ======================================================================
; File page
; ======================================================================
FDRAW:  CALL    CLRLST
        SYSSUK  STRDIS
        DB      120
        DB      2
        DB      OPTFB
        DW      TFILES
        XOR     A
        LD      (V_CNT),A
        LD      B,0             ; row
FDROW:  PUSH    BC
        LD      A,(V_POS)
        ADD     A,B
        CALL    RDENT           ; Z: name in reply; NZ: end of directory
        POP     BC
        JR      NZ,FDBLNK
        LD      HL,FNRDATA
        PUSH    BC
        LD      B,NAMELEN
        CALL    CPYSAN
        POP     BC
        LD      A,(V_CNT)
        INC     A
        LD      (V_CNT),A
        PUSH    BC
        CALL    ROWY
        LD      A,(V_CUR)
        CP      B
        LD      C,OPTFB
        JR      NZ,FDOPT
        LD      C,OPTHI
FDOPT:  CALL    SHOWROW
        POP     BC
        INC     B
        LD      A,B
        CP      NROWS
        JR      C,FDROW
FDBLNK:                         ; rows past EOF stay cleared by CLRLST

; Keep the cursor on a real entry.
        LD      A,(V_CNT)
        OR      A
        JR      NZ,FDK0
        LD      A,(V_POS)       ; empty page: step back if we paged past end
        OR      A
        JP      Z,HOSTPG        ; empty root: back to hosts
        SUB     NROWS
        JR      NC,FDSET
        XOR     A
FDSET:  LD      (V_POS),A
        JR      FDRAW
FDK0:   LD      B,A             ; A = count
        LD      A,(V_CUR)
        CP      B
        JR      C,FKEY
        LD      A,B
        DEC     A
        LD      (V_CUR),A
        JR      FDRAW

FKEY:   CALL    GETKEY
        CP      BUP
        JR      NZ,FK1
        LD      A,(V_CUR)
        OR      A
        JR      Z,FKEY
        DEC     A
        LD      (V_CUR),A
        JP      FDRAW
FK1:    CP      BDOWN
        JR      NZ,FK2
        LD      A,(V_CUR)
        INC     A
        LD      (V_CUR),A
        JP      FDRAW
FK2:    CP      BLEFT
        JR      NZ,FK3
        LD      A,(V_POS)
        SUB     NROWS
        JR      NC,FKSETP
        XOR     A
FKSETP: LD      (V_POS),A
        XOR     A
        LD      (V_CUR),A
        JP      FDRAW
FK3:    CP      BRIGHT
        JR      NZ,FK4
        LD      A,(V_CNT)
        CP      NROWS           ; only page forward off a full page
        JP      C,FKEY
        LD      A,(V_POS)
        ADD     A,NROWS
        LD      (V_POS),A
        XOR     A
        LD      (V_CUR),A
        JP      FDRAW
FK4:    CP      BTRIG
        JP      NZ,FKEY

; ======================================================================
; Boot the selected entry
; ======================================================================
; Re-read at full length; the displayed name was crunched to NAMELEN.
        LD      A,(V_POS)
        LD      HL,V_CUR
        ADD     A,(HL)
        CALL    RDPOS
        LD      A,FNDFUJI
        LD      E,FCRDIR
        LD      L,2
        CALL    FNBEGIN
        LD      A,FULLLEN
        CALL    FNPARB
        XOR     A
        CALL    FNPARB
        LD      B,8
        CALL    FNCOMMIT
        LD      HL,EENTRY
        JP      C,FAILT
        JP      NZ,FAILE

; ENTBUF = "/" + name.
        LD      HL,ENTBUF
        LD      (HL),'/'
        INC     HL
        LD      DE,FNRDATA
EBCP:   LD      A,(DE)
        LD      (HL),A
        OR      A
        JR      Z,EBDONE
        INC     HL
        INC     DE
        JR      EBCP
EBDONE:

        SYSSUK  STRDIS
        DB      2
        DB      80
        DB      OPTFB
        DW      TMOUNT

; SET_DEVICE_FULLPATH(dev, host, mode) + 256-byte padded path.
        LD      A,FNDFUJI
        LD      E,FCSDFP
        LD      L,3
        CALL    FNBEGIN
        LD      A,DEVSLOT
        CALL    FNPARB
        LD      A,(V_HOST)
        CALL    FNPARB
        LD      A,FMREAD
        CALL    FNPARB
        LD      HL,ENTBUF
        LD      C,0
SDFP1:  LD      A,(HL)
        OR      A
        JR      Z,SDFP2
        CALL    FNTXBYT
        INC     HL
        INC     C
        JR      SDFP1
SDFP2:  LD      B,C
SDFP3:  XOR     A
        CALL    FNTXBYT
        INC     B
        JR      NZ,SDFP3
        LD      B,8
        CALL    FNCOMMIT
        LD      HL,EPATH
        JP      C,FAILT
        JP      NZ,FAILE
        LD      A,(FNREPLY)
        CP      FCACK
        LD      HL,EPATH
        JP      NZ,FAILN

; MOUNT_IMAGE: the ESP32 pulls the file and pushes it to the cart while
; this transaction is outstanding.
        LD      A,FNDFUJI
        LD      E,FCMIMG
        LD      L,2
        CALL    FNBEGIN
        LD      A,DEVSLOT
        CALL    FNPARB
        LD      A,FMREAD
        CALL    FNPARB
        LD      B,120
        CALL    FNCOMMIT
        LD      HL,EMOUNT
        JP      C,FAILT
        JP      NZ,FAILE
        LD      A,(FNREPLY)
        CP      FCACK
        LD      HL,EMOUNT
        JP      NZ,FAILN

WAITRD: LD      A,(FNBSTAT)
        CP      FBREADY
        JR      Z,READY
        CP      FBFAIL
        LD      HL,ESTAGE
        JP      NC,FAILE2
        JR      WAITRD

READY:  LD      C,FRBOOTL
        LD      A,FNBLMAG
        CALL    FNREGWR
        LD      HL,STUBSRC
        LD      DE,STUB
        LD      BC,STUBLEN
        LDIR
        JP      STUB

STUBSRC:
        LD      A,(FNSWAP)
        JP      0
STUBLEN EQU     $-STUBSRC

; ======================================================================
; Directory helpers
; ======================================================================

; RDPOS: SET_DIRECTORY_POSITION(A).
RDPOS:  PUSH    AF
        LD      A,FNDFUJI
        LD      E,FCSDPS
        LD      L,1
        CALL    FNBEGIN
        POP     AF
        CALL    FNPARB
        LD      B,8
        CALL    FNCOMMIT
        RET

; RDENT: read directory entry A (display width). Z = a name is in the
; reply window; NZ = end of directory (two 7FH bytes) or error.
; A position past the end NAKs SET_DIRECTORY_POSITION (and a follow-up
; READ_DIR_ENTRY would return ".."), so that reply is checked too.
RDENT:  CALL    RDPOS
        JR      C,RDEBAD
        LD      A,(FNREPLY)
        CP      FCACK
        JR      NZ,RDEBAD
        LD      A,FNDFUJI
        LD      E,FCRDIR
        LD      L,2
        CALL    FNBEGIN
        LD      A,NAMELEN
        CALL    FNPARB
        XOR     A
        CALL    FNPARB
        LD      B,8
        CALL    FNCOMMIT
        JR      C,RDEBAD
        OR      A
        JR      NZ,RDEBAD
        LD      A,(FNREPLY)
        CP      FCACK
        JR      NZ,RDEBAD
        LD      A,(FNRDATA)     ; EOF is exactly two 7FH bytes
        CP      7FH
        JR      NZ,RDEOK
        LD      A,(FNRDATA+1)
        CP      7FH
        JR      Z,RDEBAD
RDEOK:  XOR     A               ; Z: real entry
        RET
RDEBAD: OR      1               ; NZ: EOF or error
        RET

; ======================================================================
; UI helpers
; ======================================================================

; ROWY: D = screen y for row B.
ROWY:   LD      A,B
        ADD     A,A
        ADD     A,A
        ADD     A,A
        ADD     A,14
        LD      D,A
        RET

; SHOWROW: LINBUF at x=8, y=D, options C.
SHOWROW:
        LD      E,8
        LD      HL,LINBUF
        SYSTEM  STRDIS
        RET

; CLRLST: blank the list area (rows 14..78).
CLRLST: SYSTEM  INTPC
        DO      FILL
        DW      NORMEM+14*BYTEPL
        DW      66*BYTEPL
        DB      0
        EXIT
        RET

; GETKEY: wait for a fresh player 1 handle press; A = BUP/.../BTRIG.
; Waits for release first, so holding a direction steps one row per press.
GETKEY: IN      A,(SW0)
        AND     BUP+BDOWN+BLEFT+BRIGHT+BTRIG
        JR      NZ,GETKEY       ; wait for release
GK1:    IN      A,(SW0)
        AND     BUP+BDOWN+BLEFT+BRIGHT+BTRIG
        JR      Z,GK1           ; wait for a press
        LD      B,A             ; keep only the lowest set bit, so a
        NEG                     ; diagonal cannot look like two keys
        AND     B
        RET

; ======================================================================
; Errors
; ======================================================================
NOCARD: SYSSUK  STRDIS
        DB      2
        DB      14
        DB      OPTFB
        DW      ENOCART
        JR      HALTE

FAILT:  LD      A,0FFH
        JR      FAIL
FAILE:  JR      FAIL
FAILN:  LD      A,0FEH
        JR      FAIL
FAILE2: LD      A,(FNBERR)
FAIL:   PUSH    AF
        PUSH    HL
        LD      D,80
        LD      E,2
        LD      C,OPTFB
        POP     HL
        SYSTEM  STRDIS
        POP     AF
        LD      DE,80+80*256
        CALL    HEXAT
HALTE:  JR      HALTE

; ======================================================================
; Data
; ======================================================================
TSTR:   DB      "FUJINET CONFIG",0
THOSTS: DB      "HOSTS",0
TFILES: DB      "FILES",0
TEMPTY: DB      "--------",0
TMOUNT: DB      "MOUNTING...",0
ENOCART: DB     "NO FUJINET CART",0
EHOSTS: DB      "HOSTS ERR",0
EMHOST: DB      "HOST ERR",0
EODIR:  DB      "DIR ERR",0
EENTRY: DB      "ENTRY ERR",0
EPATH:  DB      "PATH ERR",0
EMOUNT: DB      "MOUNT ERR",0
ESTAGE: DB      "STAGE ERR",0
PALET:  DB      0E7H,40H,20H,00H
        DB      0E7H,40H,20H,00H

        INCLUDE "fujilib.inc"
        INCLUDE "fujidisp.inc"
