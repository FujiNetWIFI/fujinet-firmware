; fujitest.asm -- FujiNet cartridge link probe for the Bally Astrocade.
;
; Milestone 1 of the bring-up, and the EPROM diagnostic: burned to a plain
; 27C64 it shows NO FUJINET CART with the two bytes it read instead of the
; magic, proving the header, toolchain, HVGLIB subset and screen path on
; real iron with no link hardware at all. On the FujiNet cart (or the MAME
; model) it runs GET_ADAPTERCONFIG_EXTENDED and prints the SSID, firmware
; version and IP address.
;
; Assemble: zmac -o fujitest.bin fujitest.asm

        INCLUDE "HVGLIB.H"      ; equates only; fujilib.inc carries code and
                                ; is included at the end, inside cart space

; Screen RAM 4000H-4FFFH is the only RAM. 96 visible lines use 4000H-4EFFH;
; our variables and stack sit above that, below the OS cells at 4FCEH+.
LINBUF  EQU     4F00H           ; 40-byte display string buffer
HEXBUF  EQU     4F30H           ; "XX",0
STACK   EQU     4FC0H

LINES   EQU     96              ; visible scan lines
OPTFB   EQU     0CH             ; STRDIS options: fg color 3, bg color 0

        ORG     FIRSTC
        DB      55H             ; sentinel: a menued cartridge
        DW      MENUST          ; next menu entry: the on-board list
        DW      PRGNAM
        DW      PRGSTR
PRGNAM: DB      "FUJINET TEST"
        DB      0

PRGSTR: DI                      ; polled operation, no interrupts: I stays 0
        LD      SP,STACK
        SYSTEM  INTPC
        DO      SETOUT
        DB      LINES*2         ; vertical blank line
        DB      0               ; color boundary: right palette everywhere
        DB      8               ; interrupt mode
        DO      COLSET
        DW      PALET
        DO      FILL
        DW      NORMEM
        DW      LINES*BYTEPL
        DB      0               ; black
        DO      STRDIS
        DB      2
        DB      2
        DB      OPTFB
        DW      TSTR
        EXIT

        CALL    FNCHECK
        JR      Z,FOUND

; ---- no cartridge magic: the EPROM path -------------------------------
        SYSSUK  STRDIS
        DB      2
        DB      14
        DB      OPTFB
        DW      NOCART
        LD      A,(FNMAGF)      ; show what was there instead
        LD      DE,2+24*256     ; D=y, E=x
        CALL    HEXAT
        LD      A,(FNMAGN)
        LD      DE,20+24*256
        CALL    HEXAT
HALT1:  JR      HALT1

; ---- cartridge present: talk to the FujiNet ---------------------------
FOUND:  SYSSUK  STRDIS
        DB      2
        DB      14
        DB      OPTFB
        DW      CARTOK
        LD      A,(FNPVER)
        LD      DE,124+14*256
        CALL    HEXAT

        LD      A,FNDFUJI       ; device 70H
        LD      E,FCACFGX       ; GET_ADAPTERCONFIG_EXTENDED
        LD      L,0             ; no parameters, no payload
        CALL    FNBEGIN
        LD      B,8             ; ~16 s: covers ESP32 enumeration
        CALL    FNCOMMIT
        JR      C,TMOUT
        OR      A
        JR      NZ,LERR

; Reply slice 0 carries everything we show: ssid at +0, fn_version at +125,
; sLocalIP at +140 (AdapterConfigExtended, lib/device/fujiDevice/fujiDevice.h).
        LD      HL,FNRDATA+0
        LD      B,32
        CALL    CPYSAN
        LD      A,26
        CALL    SHOWLIN

        LD      HL,FNRDATA+125
        LD      B,15
        CALL    CPYSAN
        LD      A,36
        CALL    SHOWLIN

        LD      HL,FNRDATA+140
        LD      B,16
        CALL    CPYSAN
        LD      A,46
        CALL    SHOWLIN

        SYSSUK  STRDIS
        DB      2
        DB      60
        DB      OPTFB
        DW      DONE
HALT2:  JR      HALT2

TMOUT:  SYSSUK  STRDIS
        DB      2
        DB      26
        DB      OPTFB
        DW      TOSTR
        JR      HALT2

LERR:   PUSH    AF
        SYSSUK  STRDIS
        DB      2
        DB      26
        DB      OPTFB
        DW      ERRSTR
        POP     AF
        LD      DE,70+26*256
        CALL    HEXAT
        JR      HALT2

        INCLUDE "fujilib.inc"
        INCLUDE "fujidisp.inc"

; ---- data --------------------------------------------------------------
TSTR:   DB      "FUJINET CART TEST",0
NOCART: DB      "NO FUJINET CART",0
CARTOK: DB      "CART OK  PROTO",0
TOSTR:  DB      "LINK TIMEOUT",0
ERRSTR: DB      "LINK ERR",0
DONE:   DB      "DONE",0
PALET:  DB      0E7H,40H,20H,00H        ; left palette (unused: HORCB 0)
        DB      0E7H,40H,20H,00H        ; right: white/green/blue/black
