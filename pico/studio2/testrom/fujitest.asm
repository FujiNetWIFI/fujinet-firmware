; fujitest.asm -- M1: one real transaction, and proof the cart's interlock
; survives the console's CLEAR.
;
; Asks the FujiNet for GET_ADAPTERCONFIG_EXTENDED and puts the live SSID, IP
; address and firmware version on screen, straight from the reply window (the
; cart's text engine prints it; the bytes never cross the CPU). Then SEQ and
; ERR: after CLEAR the client runs again and its sequence number must carry
; on from the cart's ACKSEQ (emu/resettest.lua).
;
; The last row is hardware check #1: for each 4K block x = 1..F, whether
; $x200-$x20F reads the BIOS's $0200-$020F ('M', a mirror: the BIOS ROMs
; decode only A10-A11) or not ('.'). The cart never drives those pages.

        CPU     1802
        INCLUDE "fujinet.inc"
        INCLUDE "s2macro.inc"

; Field offsets in GET_ADAPTERCONFIG_EXTENDED (lib/device/fujiDevice/
; fujiDevice.h, packed).
AC_SSID EQU     0                       ; char[33]
AC_VER  EQU     125                     ; char[15]
AC_SIP  EQU     140                     ; char[16], the IP as text

V_ERR   EQU     V_APP                   ; the transaction's verdict
V_BLK   EQU     V_APP+1                 ; the mirror probe's 4K block

        ORG     0400H
        INCLUDE "s2call.inc"
        INCLUDE "fujilib.inc"
        INCLUDE "fujidisp.inc"

; The runtime fills $0400-$07FB; the client's own code starts at $0C00.
        ORG     0C00H

PUTS    MACRO   rc, str
        LDI     rc
        FCALL   d_at
        LDR     R8, str
        FCALL   d_puts
        ENDM

        FIT     120
main:   FCALL   d_cls
        PUTS    00H, t_title
        FCALL   fn_chk
        BDF     have
        PUTS    20H, t_nocart
        LDI     40H
        FCALL   d_at
        PEEK    FN_MAGIC
        FCALL   d_hex
        PEEK    FN_MAGIC+1
        FCALL   d_hex
        FJMP    idle

have:   LDI     FNDEVF
        PHI     R9
        LDI     FNCADPX
        PLO     R9
        LDI     0
        FCALL   fn_beg
        FCALL   fn_go
        BNZ     fail
        FCALL   fn_ack
fail:   PLO     RA
        LDR     R8, V_ERR
        GLO     RA
        STR     R8
        BZ      show
        PUTS    20H, t_failed
        LDR     R8, V_ERR
        LDN     R8
        FCALL   d_hex
        FJMP    seq
show:   FJMP    show1

        FIT     120
show1:  PUTS    10H, t_ssid
        LDI     20H
        FCALL   d_at
        LDI     TOP_MARQUEE
        FCALL   d_op
        LDR     R8, AC_SSID
        LDI     32
        FCALL   d_rpl
        PUTS    30H, t_ip
        LDI     40H
        FCALL   d_at
        LDR     R8, AC_SIP
        LDI     15
        FCALL   d_rpl
        PUTS    50H, t_fw
        LDI     60H
        FCALL   d_at
        LDR     R8, AC_VER
        LDI     15
        FCALL   d_rpl
        FJMP    seq

        FIT     100
seq:    PUTS    70H, t_seq
        PEEK    FN_ACKSEQ
        FCALL   d_hex
        PUTS    78H, t_err
        PEEK    FN_ERR
        FCALL   d_hex
        FCALL   probe
idle:   LDI     0F0H                    ; the frame count, bottom right
        BR      idle1
idle1:  LDI     9CH
        FCALL   d_at
        GHI     RE
        FCALL   d_hex
        GLO     RE
        FCALL   d_hex
        BR      idle1

; probe -- row 8: 'M' where $x200-$x20F equals the BIOS's $0200-$020F.
        FIT     80
probe:  LDI     80H
        FCALL   d_at
        LDR     R8, V_BLK
        LDI     10H
        STR     R8
prb1:   LDR     R8, V_BLK
        LDN     R8
        ADI     02H                     ; R8 = $x200
        PHI     R8
        LDI     0
        PLO     R8
        LDR     R9, 0200H
prb2:   LDN     R9
        STR     R2
        LDA     R8
        XOR
        BNZ     prb3                    ; differs: not a mirror
        INC     R9
        GLO     R9
        XRI     10H
        BNZ     prb2
        LDI     'M'
        BR      prb4
prb3:   LDI     '.'
prb4:   FCALL   d_putc
        LDR     R8, V_BLK
        LDN     R8
        ADI     10H
        STR     R8
        BNZ     prb1
        FRET

t_title:  DB    "FUJINET STUDIO2", 0
t_nocart: DB    "NO FUJINET CART", 0
t_failed: DB    "FAILED: E", 0
t_ssid:   DB    "SSID", 0
t_ip:     DB    "IP", 0
t_fw:     DB    "FW", 0
t_seq:    DB    "SEQ ", 0
t_err:    DB    "ERR ", 0

        S2CLAIM
