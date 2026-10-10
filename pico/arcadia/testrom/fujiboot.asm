; fujiboot.asm -- mount a cartridge image over the network and boot it.
;
; Milestone 2: MOUNT_HOST -> SET_DEVICE_FULLPATH -> MOUNT_IMAGE. The last
; one makes the ESP32 side stream the image back to the cartridge on the
; same link, addressed to the DBC device, while MOUNT_IMAGE's own reply is
; still outstanding; the cartridge stages it and reports BOOT_STATE.
;
; The boot itself: arm the swap (BOOTLOCK), run an 11-byte stub from screen
; RAM ($18E0) that clears the PSW and reads the FN_HOT_SWAP hotspot -- the
; cart flips to the staged image between that read and the next -- then
; BCTA $0000. The new image's own header runs.
;
; The target host slot and path are baked in by build.sh:
;   BOOT_HOST=0 BOOT_PATH=/game.bin ./build.sh fujiboot

        CPU     2650

DEVSLOT EQU     0
STUB    EQU     $18E0           ; the swap stub, in free RAM
STBPTA  EQU     $18E9           ; where the FNSWAP pointer lands at runtime

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
        BSTA,UN DCLS

        SETSTR  TSTR
        LODI,R2 $FF
        BSTA,UN DPRINT

        BSTA,UN FNCHECK
        BCTR,EQ HAVE
        SETSTR  ENOC
        LODI,R2 $20-1
        BSTA,UN DPRINT
HLTNC:  BCTA,UN HLTNC

HAVE:   SETSTR  BOOTPTH         ; show the target path on row 1
        LODI,R2 $10-1
        BSTA,UN DPRINT

; ---- MOUNT_HOST(host) --------------------------------------------------
        BSTA,UN FNSTART
        LODI,R1 FRCMD
        LODI,R2 FCMHOST
        BSTA,UN FNREGWR
        LODI,R1 FRNPARM
        LODI,R2 1
        BSTA,UN FNREGWR
        LODI,R2 BOOTHST
        BSTA,UN FNPARB
        BSTA,UN FNCOMMIT
        LODI,R1 1               ; err class 1 = host
        BSTA,UN CHKACK

; ---- SET_DEVICE_FULLPATH(dev, host, mode, path) ------------------------
        BSTA,UN FNSTART
        LODI,R1 FRCMD
        LODI,R2 FCSDFP
        BSTA,UN FNREGWR
        LODI,R1 FRNPARM
        LODI,R2 3
        BSTA,UN FNREGWR
        LODI,R2 DEVSLOT
        BSTA,UN FNPARB
        LODI,R2 BOOTHST
        BSTA,UN FNPARB
        LODI,R2 FMREAD
        BSTA,UN FNPARB
        SETFSTR BOOTPTH
        BSTA,UN FNPATH          ; path, NUL-padded to 256
        BSTA,UN FNCOMMIT
        LODI,R1 2               ; err class 2 = path
        BSTA,UN CHKACK

; ---- MOUNT_IMAGE(dev, mode) --------------------------------------------
        SETSTR  MNTG
        LODI,R2 $20-1
        BSTA,UN DPRINT
        BSTA,UN FNSTART
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
        LODI,R1 3               ; err class 3 = mount
        BSTA,UN CHKACK

; ---- wait for the staged image (progress bar on row 3) -----------------
WAITRD: LODA,R0 *FBSTP
        COMI,R0 FBREADY
        BCTR,EQ READY
        LODA,R0 *FBSTP
        COMI,R0 FBFAIL
        BCFR,LT WSHOW           ; < FBFAIL: still working
        LODI,R1 4               ; stage failure
        LODA,R0 *FBERP
        STRZ    R2
        BCTA,UN FAIL
WSHOW:  LODA,R0 *FBPCP          ; 0-100 -> a 10-cell bar on row 3
        BSTA,UN BAR
        BCTR,UN WAITRD

; ---- ready: arm and swap ----------------------------------------------
READY:  SETSTR  BOOTG
        LODI,R2 $30-1
        BSTA,UN DPRINT
        LODI,R1 FRBOOTL         ; arm the swap
        LODI,R2 FNBLMAG
        BSTA,UN FNREGWR
        LODI,R3 STUBLEN-1       ; copy the stub into RAM
STCPY:  LODA,R0 STUBSRC,R3
        STRA,R0 STUB,R3
        BDRR,R3 STCPY
        LODA,R0 STUBSRC
        STRA,R0 STUB
        BCTA,UN STUB

; The stub, assembled here but run from $18E0. The FNSWAP pointer must land
; at STBPTA ($18E9), so the load reaches the RAM copy: 9 bytes precede it.
STUBSRC:
        EORZ    R0              ; +0
        LPSU                    ; +1  PSU = 0
        LPSL                    ; +2  PSL = 0 (power-on-like for the image)
        LODA,R0 *STBPTA         ; +3  read $2DFE indirect -> swap
        BCTA,UN $0000           ; +6  the new image's header
        DWBE    FNSWAP          ; +9  = $18E9 at runtime
STUBLEN EQU     $-STUBSRC

; ---- helpers -----------------------------------------------------------

; After FNCOMMIT: R0 = FNERR (or FETIMO). R1 = error class. On timeout,
; error, or NAK, jump to FAIL with R1=class and R2=detail; else return.
; Calls one down. Clobbers R0,R2.
CHKACK: COMI,R0 0
        BCFR,EQ CKBAD           ; nonzero FNERR (incl FETIMO)
        LODA,R0 *FREPP
        COMI,R0 FCACK
        BCFR,EQ CKNAK
        RETC,UN
CKBAD:  STRZ    R2              ; detail = FNERR value
        BCTA,UN FAIL
CKNAK:  LODI,R2 $EE             ; detail = NAK marker
        BCTA,UN FAIL

; Draw a 10-cell progress bar on row 3 for percentage R0 (0-100).
; Leaf. Clobbers R0,R1,R2,R3.
BAR:    LODI,R1 0               ; filled cells = R0 / 10
BARDIV: COMI,R0 10
        BCTR,LT BARDRW
        SUBI,R0 10
        ADDI,R1 1
        BCTR,UN BARDIV
BARDRW: LODI,R3 0               ; cell index
BARC:   LODZ    R3              ; fill while index R3 < filled R1
        COMZ    R1
        BCTR,LT BARFIL
        LODI,R0 GBLINE
        BCTR,UN BARPUT
BARFIL: LODI,R0 GBLOCK
BARPUT: STRA,R0 SCREEN+48,R3    ; row 3
        ADDI,R3 1
        COMI,R3 10
        BCFR,EQ BARC
        RETC,UN

; R1 = error class, R2 = detail byte. Show "ERR c dd" on row 5 and halt.
FAIL:   LODZ    R1              ; stash across the clobbering DPRINT
        STRA,R0 FTMPA
        LODZ    R2
        STRA,R0 FTMPB
        SETSTR  ESTR
        LODI,R2 $50-1
        BSTA,UN DPRINT          ; "ERR "
        LODA,R0 FTMPA
        ADDI,R0 $10             ; class as a single digit
        STRA,R0 SCREEN+$54
        LODI,R2 $56-1
        LODA,R0 FTMPB
        BSTA,UN DHEX
HLTF:   BCTA,UN HLTF

FTMPA   EQU     $18DE           ; zone A scratch (V_TMP2)
FTMPB   EQU     $18DF

; ---- data --------------------------------------------------------------
TSTR:   DB      "FUJINET BOOT",0
MNTG:   DB      "MOUNTING",0
BOOTG:  DB      "BOOTING",0
ENOC:   DB      "NO FUJINET CART",0
ESTR:   DB      "ERR ",0

        INCLUDE "../build/bootcfg.inc"
