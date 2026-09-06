; fujicfg.asm -- FujiNet CONFIG for the Emerson Arcadia 2001.
;
; Milestone 3: pick a host slot, browse the root of its tree, pick an image,
; boot it. Controls (see fujidisp.inc SCAN): the P1 disc moves up/down and
; the FIRE button selects; the keypad is the alternative -- 8 down, 4/6
; page, Enter select, Clear back. (FIRE and keypad '2' are the same wire, so
; that bit selects rather than moving.) The console RESET button restarts
; the client; the cart keeps its state, so sequence numbers derive from
; ACKSEQ (see fujilib.inc).
;
; Root listing only (the o2/astrocade testrom scope). Display names are
; crunched to the row width, so the boot path re-reads the selected entry
; at full length first.

        CPU     2650

DEVSLOT EQU     0
NROWS   EQU     8               ; list rows (screen rows 2..9)
NAMELEN EQU     14              ; display width (chevron col 0, name col 1..14)
FULLLEN EQU     120             ; re-read width for the boot path
STUB    EQU     $18E0
STBPTA  EQU     $18E9

; RAM (zone A + zone B)
V_HOST  EQU     $18D1
V_CUR   EQU     $18D2
V_CNT   EQU     $18D3
V_POS   EQU     $18D4
V_ROW   EQU     $18D5
SELIDX  EQU     $18D6           ; row LROW should mark with the chevron
FTMPA   EQU     $18DE
FTMPB   EQU     $18DF
V_PATH  EQU     $1A00           ; "/" + entry name, NUL-terminated

        ORG     $0000
        BCTA    UN,START
        DB      $17

        ORG     $0020
        INCLUDE "fujidisp.inc"
        INCLUDE "fujilib.inc"

START:  EORZ    R0
        LPSU
        LODI,R0 $02             ; COM=1: logical compares
        LPSL
        BSTA,UN DINIT
        BSTA,UN FNCHECK
        BCTA,EQ HOSTPG
        BSTA,UN DCLS
        SETSTR  ENOC
        LODI,R2 $20-1
        BSTA,UN DPRINT
HLTNC:  BCTA,UN HLTNC

; ======================================================================
; Host page: READ_HOST_SLOTS (8 x 32 bytes = slice 0)
; ======================================================================
HOSTPG: EORZ    R0
        STRA,R0 V_HOST
        BSTA,UN FNSTART
        LODI,R1 FRCMD
        LODI,R2 FCRHSL
        BSTA,UN FNREGWR
        LODI,R1 FRNPARM
        LODI,R2 0
        BSTA,UN FNREGWR
        BSTA,UN FNCOMMIT
        COMI,R0 0
        BCFA,EQ EHOSTS
        LODI,R2 0
        BSTA,UN FNSLICE

HDRAW:  BSTA,UN DCLS
        SETSTR  THOSTS
        LODI,R2 $FF
        BSTA,UN DPRINT
        LODA,R0 V_HOST          ; chevron marks the selected host
        STRA,R0 SELIDX
        EORZ    R0
        STRA,R0 V_ROW
HDROW:  LODA,R0 V_ROW           ; DSTRP = FNRDATA + slot*32
        ADDZ    R0
        ADDZ    R0
        ADDZ    R0
        ADDZ    R0
        ADDZ    R0              ; slot*32
        STRA,R0 DSTRP+1
        LODI,R0 (FNRDATA>>8)
        STRA,R0 DSTRP
        LODA,R0 *DSTRP          ; first name char: 0 => empty slot
        COMI,R0 0
        BCFA,EQ HDNAME
        SETSTR  TEMPTY
HDNAME: LODA,R0 V_ROW
        BSTA,UN LROW
        LODA,R0 V_ROW
        ADDI,R0 1
        STRA,R0 V_ROW
        COMI,R0 8
        BCFA,EQ HDROW

HKEY:   BSTA,UN GETKEY
        COMI,R0 KUP
        BCFA,EQ HK1
        LODA,R0 V_HOST
        COMI,R0 0
        BCTA,EQ HKEY
        SUBI,R0 1
        STRA,R0 V_HOST
        BCTA,UN HDRAW
HK1:    COMI,R0 KDOWN
        BCFA,EQ HK2
        LODA,R0 V_HOST
        COMI,R0 7
        BCTA,EQ HKEY
        ADDI,R0 1
        STRA,R0 V_HOST
        BCTA,UN HDRAW
HK2:    COMI,R0 KSEL
        BCFA,EQ HKEY
        BCTA,UN OPENH

EHOSTS: SETSTR  EHS
        BCTA,UN FAIL

; ======================================================================
; Open the host and its root directory
; ======================================================================
OPENH:  BSTA,UN FNSTART         ; MOUNT_HOST(host)
        LODI,R1 FRCMD
        LODI,R2 FCMHOST
        BSTA,UN FNREGWR
        LODI,R1 FRNPARM
        LODI,R2 1
        BSTA,UN FNREGWR
        LODA,R0 V_HOST
        STRZ    R2
        BSTA,UN FNPARB
        BSTA,UN FNCOMMIT
        BSTA,UN NEEDACK

        BSTA,UN FNSTART         ; OPEN_DIRECTORY(host, "/")
        LODI,R1 FRCMD
        LODI,R2 FCODIR
        BSTA,UN FNREGWR
        LODI,R1 FRNPARM
        LODI,R2 1
        BSTA,UN FNREGWR
        LODA,R0 V_HOST
        STRZ    R2
        BSTA,UN FNPARB
        SETFSTR SLASH
        BSTA,UN FNPATH
        BSTA,UN FNCOMMIT
        BSTA,UN NEEDACK

        EORZ    R0
        STRA,R0 V_POS
        STRA,R0 V_CUR

; ======================================================================
; File page
; ======================================================================
FDRAW:  BSTA,UN DCLS
        SETSTR  TFILES
        LODI,R2 $FF
        BSTA,UN DPRINT
        LODA,R0 V_CUR           ; chevron marks the cursor row
        STRA,R0 SELIDX
        EORZ    R0
        STRA,R0 V_CNT
        STRA,R0 V_ROW
FDROW:  LODA,R0 V_POS
        STRZ    R1
        LODA,R0 V_ROW
        ADDZ    R1              ; R0 = V_POS + row
        STRZ    R2
        BSTA,UN RDENT
        COMI,R0 0
        BCFA,EQ FDDONE
        SETSTR  FNRDATA
        LODA,R0 V_CNT
        ADDI,R0 1
        STRA,R0 V_CNT
        LODA,R0 V_ROW
        BSTA,UN LROW
        LODA,R0 V_ROW
        ADDI,R0 1
        STRA,R0 V_ROW
        COMI,R0 NROWS
        BCFA,EQ FDROW
FDDONE: LODA,R0 V_CNT           ; keep the cursor on a real entry
        COMI,R0 0
        BCFA,EQ FDCUR
        LODA,R0 V_POS           ; empty page
        COMI,R0 0
        BCTA,EQ HOSTPG
        SUBI,R0 NROWS
        STRA,R0 V_POS
        BCTA,UN FDRAW
FDCUR:  STRZ    R1              ; R1 = count
        LODA,R0 V_CUR
        COMZ    R1
        BCTA,LT FKEY            ; cursor < count
        LODZ    R1
        SUBI,R0 1
        STRA,R0 V_CUR
        BCTA,UN FDRAW

FKEY:   BSTA,UN GETKEY
        COMI,R0 KUP
        BCFA,EQ FK1
        LODA,R0 V_CUR
        COMI,R0 0
        BCTA,EQ FKEY
        SUBI,R0 1
        STRA,R0 V_CUR
        BCTA,UN FDRAW
FK1:    COMI,R0 KDOWN
        BCFA,EQ FK2
        LODA,R0 V_CUR
        ADDI,R0 1
        STRZ    R1
        LODA,R0 V_CNT
        COMZ    R1              ; new cursor < count ?
        BCTA,LT FKEY
        LODZ    R1
        STRA,R0 V_CUR
        BCTA,UN FDRAW
FK2:    COMI,R0 KPGUP
        BCFA,EQ FK3
        LODA,R0 V_POS
        COMI,R0 NROWS
        BCTA,LT FKEY
        SUBI,R0 NROWS
        STRA,R0 V_POS
        EORZ    R0
        STRA,R0 V_CUR
        BCTA,UN FDRAW
FK3:    COMI,R0 KPGDN
        BCFA,EQ FK4
        LODA,R0 V_CNT
        COMI,R0 NROWS
        BCTA,LT FKEY            ; partial page: no next
        LODA,R0 V_POS
        ADDI,R0 NROWS
        STRA,R0 V_POS
        EORZ    R0
        STRA,R0 V_CUR
        BCTA,UN FDRAW
FK4:    COMI,R0 KBACK
        BCFA,EQ FK5
        BCTA,UN HOSTPG
FK5:    COMI,R0 KSEL
        BCFA,EQ FKEY
        BCTA,UN BOOTIT

; ======================================================================
; Boot the selected entry
; ======================================================================
BOOTIT: LODA,R0 V_POS           ; re-read at full length
        STRZ    R1
        LODA,R0 V_CUR
        ADDZ    R1
        STRZ    R2
        BSTA,UN RDPOS
        BSTA,UN FNSTART
        LODI,R1 FRCMD
        LODI,R2 FCRDIR
        BSTA,UN FNREGWR
        LODI,R1 FRNPARM
        LODI,R2 2
        BSTA,UN FNREGWR
        LODI,R2 FULLLEN
        BSTA,UN FNPARB
        LODI,R2 0
        BSTA,UN FNPARB
        BSTA,UN FNCOMMIT
        BSTA,UN NEEDACK

        LODI,R0 '/'             ; V_PATH = "/" + name
        STRA,R0 V_PATH
        LODI,R3 0
BPCP:   LODA,R0 *FRDP,R3        ; name[R3]
        STRA,R0 V_PATH+1,R3
        COMI,R0 0
        BCTA,EQ BPGO
        ADDI,R3 1
        BCTA,UN BPCP
BPGO:   BSTA,UN DCLS
        SETSTR  TMNT
        LODI,R2 $20-1
        BSTA,UN DPRINT

        BSTA,UN FNSTART         ; SET_DEVICE_FULLPATH(dev, host, mode) + path
        LODI,R1 FRCMD
        LODI,R2 FCSDFP
        BSTA,UN FNREGWR
        LODI,R1 FRNPARM
        LODI,R2 3
        BSTA,UN FNREGWR
        LODI,R2 DEVSLOT
        BSTA,UN FNPARB
        LODA,R0 V_HOST
        STRZ    R2
        BSTA,UN FNPARB
        LODI,R2 FMREAD
        BSTA,UN FNPARB
        SETFSTR V_PATH
        BSTA,UN FNPATH
        BSTA,UN FNCOMMIT
        BSTA,UN NEEDACK

        BSTA,UN FNSTART         ; MOUNT_IMAGE(dev, mode)
        LODI,R1 FRCMD
        LODI,R2 FCMIMG
        BSTA,UN FNREGWR
        LODI,R1 FRNPARM
        LODI,R2 2
        BSTA,UN FNREGWR
        LODI,R2 DEVSLOT
        BSTA,UN FNPARB
        LODI,R2 FMREAD
        BSTA,UN FNPARB
        BSTA,UN FNCOMMIT
        BSTA,UN NEEDACK

WAITRD: LODA,R0 *FBSTP
        COMI,R0 FBREADY
        BCTA,EQ READY
        LODA,R0 *FBSTP
        COMI,R0 FBFAIL
        BCFA,LT WAITRD
        SETSTR  ESTG
        BCTA,UN FAIL
READY:  LODI,R1 FRBOOTL
        LODI,R2 FNBLMAG
        BSTA,UN FNREGWR
        LODI,R3 STUBLEN-1
STCPY:  LODA,R0 STUBSRC,R3
        STRA,R0 STUB,R3
        BDRR,R3 STCPY
        LODA,R0 STUBSRC
        STRA,R0 STUB
        BCTA,UN STUB

STUBSRC:
        EORZ    R0
        LPSU
        LPSL
        LODA,R0 *STBPTA
        BCTA,UN $0000
        DWBE    FNSWAP
STUBLEN EQU     $-STUBSRC

; ======================================================================
; Helpers
; ======================================================================

; NEEDACK: after FNCOMMIT (R0 = FNERR/FETIMO). Any failure -> generic error
; + halt; else return. Calls one down. Clobbers R0,R2.
NEEDACK: COMI,R0 0
        BCFA,EQ NAKBAD
        LODA,R0 *FREPP
        COMI,R0 FCACK
        BCFA,EQ NAKBAD
        RETC,UN
NAKBAD: SETSTR  EGEN
        BCTA,UN FAIL

; RDPOS: SET_DIRECTORY_POSITION(R2). Tail-calls FNCOMMIT (R0=FNERR out).
; Clobbers R0,R1,R2.
RDPOS:  STRA,R2 FTMPA
        BSTA,UN FNSTART
        LODI,R1 FRCMD
        LODI,R2 FCSDPS
        BSTA,UN FNREGWR
        LODI,R1 FRNPARM
        LODI,R2 1
        BSTA,UN FNREGWR
        LODA,R0 FTMPA
        STRZ    R2
        BSTA,UN FNPARB
        BCTA,UN FNCOMMIT

; RDENT: read display-width entry at index R2. R0=0 if a real name is in the
; reply window, nonzero at end-of-directory or on error.
; Calls a couple down. Clobbers R0,R1,R2,R3.
RDENT:  BSTA,UN RDPOS
        COMI,R0 0
        BCFA,EQ RDBAD
        LODA,R0 *FREPP
        COMI,R0 FCACK
        BCFA,EQ RDBAD
        BSTA,UN FNSTART
        LODI,R1 FRCMD
        LODI,R2 FCRDIR
        BSTA,UN FNREGWR
        LODI,R1 FRNPARM
        LODI,R2 2
        BSTA,UN FNREGWR
        LODI,R2 NAMELEN
        BSTA,UN FNPARB
        LODI,R2 0
        BSTA,UN FNPARB
        BSTA,UN FNCOMMIT
        COMI,R0 0
        BCFA,EQ RDBAD
        LODA,R0 *FREPP
        COMI,R0 FCACK
        BCFA,EQ RDBAD
        LODA,R0 *FRDP           ; EOF = two 7F bytes
        COMI,R0 $7F
        BCFA,EQ RDOK
        LODI,R1 1
        LODA,R0 *FRDP,R1
        COMI,R0 $7F
        BCTA,EQ RDBAD
RDOK:   EORZ    R0
        RETC,UN
RDBAD:  LODI,R0 1
        RETC,UN

; LROW: draw list row R0 (0..NROWS-1). DSTRP = name; SELIDX = the row to
; mark with the chevron. Clobbers R0,R1,R2,R3.
LROW:   STRA,R0 FTMPA           ; row index
        ADDZ    R0
        ADDZ    R0
        ADDZ    R0
        ADDZ    R0              ; row*16
        ADDI,R0 32              ; base = row*16 + 32 (screen row 2 + index)
        STRA,R0 FTMPB
        STRZ    R2              ; R2 = running screen offset
        LODI,R3 16
LRCLR:  LODI,R0 0
        STRA,R0 SCREEN,R2
        ADDI,R2 1
        BDRR,R3 LRCLR
        LODA,R0 FTMPA           ; chevron if row == SELIDX
        STRZ    R1
        LODA,R0 SELIDX
        COMZ    R1
        BCFA,EQ LRNAME
        LODA,R0 FTMPB
        STRZ    R2
        LODI,R0 GCHEVR
        STRA,R0 SCREEN,R2
LRNAME: LODA,R0 FTMPB           ; R2 = base; DPRINT starts at R2+1
        STRZ    R2
        BSTA,UN DPRINT
        RETC,UN

; ======================================================================
; Errors
; ======================================================================
FAIL:   LODI,R2 $60-1           ; row 6; DSTRP set by caller
        BSTA,UN DPRINT
HLTF:   BCTA,UN HLTF

; ======================================================================
; Data
; ======================================================================
TSTR:   DB      "FUJINET CONFIG",0
THOSTS: DB      "HOSTS",0
TFILES: DB      "FILES",0
TEMPTY: DB      "--------",0
TMNT:   DB      "MOUNTING",0
ENOC:   DB      "NO FUJINET CART",0
SLASH:  DB      "/",0
EGEN:   DB      "IO ERR",0
EHS:    DB      "HOSTS ERR",0
ESTG:   DB      "STAGE ERR",0

