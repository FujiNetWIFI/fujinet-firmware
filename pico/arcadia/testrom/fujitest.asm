; fujitest.asm -- FujiNet cartridge link probe for the Emerson Arcadia 2001.
;
; Milestone 1 of the bring-up, and the EPROM diagnostic: burned to a plain
; 8K EPROM it shows NO FUJINET CART with the two bytes it read instead of
; the magic, proving the header, toolchain and screen path on real iron
; with no link hardware at all. On the FujiNet cart (or the MAME model) it
; runs GET_ADAPTERCONFIG_EXTENDED and prints the SSID, firmware version
; and IP address (dotted hex).
;
; Screen: row 0 title, rows 4-5 SSID, row 7 version, row 9 IP.
; Build: ../build.sh fujitest

        CPU     2650

        ORG     $0000
        BCTA    UN,START        ; cartridge header: jump past the
        DB      $17             ; RETC,UN interrupt guard at $0003

        ORG     $0020
        INCLUDE "fujidisp.inc"  ; macros must precede their callers (AS
        INCLUDE "fujilib.inc"   ; resolves forward labels, not macros)

START:  EORZ    R0
        LPSU
        LODI,R0 $02             ; COM=1: logical compares (fujilib relies
        LPSL                    ; on it)
        BSTA,UN DINIT
        BSTA,UN DCLS

        SETSTR  TSTR            ; title
        LODI,R2 $FF             ; row 0 col 0, minus-one convention
        BSTA,UN DPRINT

        BSTA,UN FNCHECK
        BCTR,EQ FOUND           ; R0=0: magic present (FNCHECK's final
                                ; load left CC testable)

; ---- no cartridge magic: the EPROM path -------------------------------
        SETSTR  NCSTR
        LODI,R2 $20-1           ; row 2
        BSTA,UN DPRINT
        LODA,R0 *FMAGFP         ; the two bytes living where the magic
        LODI,R2 $30-1           ; should be, row 3
        BSTA,UN DHEX
        LODA,R0 *FMAGNP
        BSTA,UN DHEX            ; R2 continues from DHEX's own advance
HALT1:  BCTA,UN HALT1

; ---- cartridge present: ask the adapter about itself ------------------
FOUND:  BSTA,UN FNSTART
        LODI,R1 FRCMD
        LODI,R2 FCACFGX
        BSTA,UN FNREGWR
        LODI,R1 FRNPARM
        LODI,R2 0
        BSTA,UN FNREGWR
        BSTA,UN FNCOMMIT
        COMI,R0 0
        BCTR,EQ GOTCFG

        STRZ    R3              ; hold the error code
        SETSTR  ERSTR
        LODI,R2 $20-1           ; row 2: ERR nn
        BSTA,UN DPRINT
        LODZ    R3
        BSTA,UN DHEX
HALT2:  BCTA,UN HALT2

; Reply slice 0 carries everything we show: ssid at +0, fn_version at +125,
; sLocalIP at +140 (AdapterConfigExtended, lib/device/fujiDevice/fujiDevice.h).
GOTCFG: LODI,R2 0
        BSTA,UN FNSLICE

        SETSTR  FNRDATA         ; SSID: flows from row 4 into row 5
        LODI,R2 $40-1
        BSTA,UN DPRINT

        SETSTR  FNRDATA+125     ; row 7: firmware version
        LODI,R2 $70-1
        BSTA,UN DPRINT

        SETSTR  FNRDATA+140     ; row 9: IP (sLocalIP is dotted-decimal
        LODI,R2 $90-1           ; TEXT in the reply, not binary octets)
        BSTA,UN DPRINT
IPDONE: BCTA,UN IPDONE

TSTR:   DB      "FUJITEST",0
NCSTR:  DB      "NO FUJINET CART",0
ERSTR:  DB      "ERR ",0
