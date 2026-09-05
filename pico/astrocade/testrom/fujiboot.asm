; fujiboot.asm -- mount a cartridge image over the network and boot it.
;
; Milestone 2: MOUNT_HOST -> SET_DEVICE_FULLPATH -> MOUNT_IMAGE. The last
; one makes the ESP32 side stream the image back to the cartridge on the
; same link, addressed to the DBC device, while MOUNT_IMAGE's own reply is
; still outstanding; the cartridge stages it and reports BOOT_STATE.
;
; The boot itself: arm the swap (BOOTLOCK), run a stub from screen RAM that
; reads the FN_HOT_SWAP hotspot -- the cart flips to the staged image
; between that read and the next -- then JP 0. The OS cold-starts, walks the
; new image's 0x55 sentinel, and the game is on the onboard menu.
;
; The target host slot and path are baked in by build.sh:
;   BOOT_HOST=0 BOOT_PATH=/game.bin ./build.sh fujiboot

        INCLUDE "HVGLIB.H"

LINBUF  EQU     4F00H
HEXBUF  EQU     4F30H
STUB    EQU     4FE0H           ; the swap stub, above our vars, below 4FF8H
STACK   EQU     4FC0H

LINES   EQU     96
OPTFB   EQU     0CH
DEVSLOT EQU     0

        ORG     FIRSTC
        DB      55H
        DW      MENUST
        DW      PRGNAM
        DW      PRGSTR
PRGNAM: DB      "FUJINET BOOT"
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

; Show the target path.
        LD      HL,BOOTPTH
        LD      B,38
        CALL    CPYSAN
        LD      A,12
        CALL    SHOWLIN

; ---- MOUNT_HOST(host) --------------------------------------------------
        LD      A,FNDFUJI
        LD      E,FCMHOST
        LD      L,1
        CALL    FNBEGIN
        LD      A,BOOTHST
        CALL    FNPARB
        LD      B,8
        CALL    FNCOMMIT
        LD      HL,EHOST
        JP      C,FAILT
        JP      NZ,FAILE
        LD      A,(FNREPLY)
        CP      FCACK
        JP      NZ,FAILN

; ---- SET_DEVICE_FULLPATH(dev, host, mode, path) ------------------------
; The payload must be the full 256 bytes NUL-padded: the firmware reads it
; into a fixed buffer and fails a short read (the o2 client's lesson).
        LD      A,FNDFUJI
        LD      E,FCSDFP
        LD      L,3
        CALL    FNBEGIN
        LD      A,DEVSLOT
        CALL    FNPARB
        LD      A,BOOTHST
        CALL    FNPARB
        LD      A,FMREAD
        CALL    FNPARB
        LD      HL,BOOTPTH      ; stream the path, then pad to 256
        LD      C,0             ; bytes sent (FNTXBYT only touches A,D,E)
SDFP1:  LD      A,(HL)
        OR      A
        JR      Z,SDFP2
        CALL    FNTXBYT
        INC     HL
        INC     C
        JR      SDFP1
SDFP2:  LD      B,C             ; pad with zeros: loop until B wraps, so
SDFP3:  XOR     A               ; 256-C of them and 256 bytes in total
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
        JP      NZ,FAILN

; ---- MOUNT_IMAGE(dev, mode) --------------------------------------------
        SYSSUK  STRDIS
        DB      2
        DB      22
        DB      OPTFB
        DW      MNTING
        LD      A,FNDFUJI
        LD      E,FCMIMG
        LD      L,2
        CALL    FNBEGIN
        LD      A,DEVSLOT
        CALL    FNPARB
        LD      A,FMREAD
        CALL    FNPARB
        LD      B,120           ; ~4 min: the whole TNFS pull + DBC push
        CALL    FNCOMMIT
        LD      HL,EMOUNT
        JP      C,FAILT
        JP      NZ,FAILE
        LD      A,(FNREPLY)
        CP      FCACK
        JP      NZ,FAILN

; ---- wait for the staged image -----------------------------------------
WAITRD: LD      A,(FNBSTAT)
        CP      FBREADY
        JR      Z,READY
        CP      FBFAIL
        LD      HL,ESTAGE
        JP      NC,FAILE2       ; FBFAIL or above
        LD      A,(FNBPCT)      ; progress, in hex -- it's a debug screen
        LD      DE,70+22*256
        CALL    HEXAT
        JR      WAITRD

READY:  SYSSUK  STRDIS
        DB      2
        DB      32
        DB      OPTFB
        DW      BOOTING

; Arm the swap, then run the stub from screen RAM: the swap replaces every
; byte of this window, so nothing may execute from the cart past the
; hotspot read.
        LD      C,FRBOOTL
        LD      A,FNBLMAG
        CALL    FNREGWR
        LD      HL,STUBSRC
        LD      DE,STUB
        LD      BC,STUBLEN
        LDIR
        JP      STUB

STUBSRC:
        LD      A,(FNSWAP)      ; the cart swaps after serving this read
        JP      0               ; OS cold start: sentinel walk, new menu
STUBLEN EQU     $-STUBSRC

; ---- errors ------------------------------------------------------------
NOCARD: SYSSUK  STRDIS
        DB      2
        DB      12
        DB      OPTFB
        DW      ENOCART
        JR      HALTE

; HL = which step failed. Timeout / FNERR / NAK variants.
FAILT:  LD      A,0FFH
        JR      FAIL
FAILE:  JR      FAIL            ; A = FNERR value
FAILN:  LD      A,0FEH          ; NAK
        JR      FAIL
FAILE2: LD      A,(FNBERR)
FAIL:   PUSH    AF
        PUSH    HL
        LD      A,42
        LD      D,A
        LD      E,2
        LD      C,OPTFB
        POP     HL
        SYSTEM  STRDIS
        POP     AF
        LD      DE,80+42*256
        CALL    HEXAT
HALTE:  JR      HALTE

; ---- data --------------------------------------------------------------
TSTR:   DB      "FUJINET BOOT",0
MNTING: DB      "MOUNTING",0
BOOTING: DB     "BOOTING",0
ENOCART: DB     "NO FUJINET CART",0
EHOST:  DB      "HOST ERR",0
EPATH:  DB      "PATH ERR",0
EMOUNT: DB      "MOUNT ERR",0
ESTAGE: DB      "STAGE ERR",0
PALET:  DB      0E7H,40H,20H,00H
        DB      0E7H,40H,20H,00H

        INCLUDE "fujilib.inc"
        INCLUDE "fujidisp.inc"
        INCLUDE "../build/bootcfg.inc"
