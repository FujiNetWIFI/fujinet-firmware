; fujitest.s -- M1: one real transaction, and proof the cart survives a
; power cycle.
;
; Asks the FujiNet for GET_ADAPTERCONFIG_EXTENDED and puts the live SSID, IP
; address and firmware version on screen. Nothing is faked: the bytes come
; off a socket to fujinet-pc, through the cartridge's own fujimail.c.
; emu/powertest.lua power-cycles the console: the cart starts the
; interlock over (ACKSEQ 0), and the client's next transaction must still go
; through.

        .include "fujinet.inc"

        .import disp_init, disp_at, disp_str, disp_hex, disp_rpl, disp_on
        .import fn_chk, fn_beg, fn_go, fn_ack
        .export main

; Field offsets inside GET_ADAPTERCONFIG_EXTENDED (lib/device/fujiDevice/
; fujiDevice.h, packed). All below 256, so one index reaches them.
AC_SSID = 0                     ; char[33]
AC_VER  = 125                   ; char[15]
AC_SIP  = 140                   ; char[16], the IP already formatted as text

        .segment "ZEROPAGE"
aperr:  .res 1

        .segment "CODE"

.macro  PUTS    row, col, str
        lda     #row
        ldx     #col
        jsr     disp_at
        lda     #<str
        sta     FN_PTR
        lda     #>str
        sta     FN_PTR+1
        jsr     disp_str
.endmacro

.proc main
        jsr     disp_init
        lda     #0
        sta     aperr
        jsr     fn_chk
        beq     have
        lda     #FNENOC
        sta     aperr
        jmp     show

have:   lda     #FNDEVF
        sta     FN_DEV
        lda     #FNCADPX
        sta     FN_CMD
        lda     #0
        sta     FN_NPR
        jsr     fn_beg
        jsr     fn_go
        sta     aperr
        cmp     #FNEOK
        bne     show
        jsr     fn_ack
        sta     aperr

show:   PUTS    2, 2, title
        lda     aperr
        beq     ok

        PUTS    5, 2, tfail
        lda     #5
        ldx     #12
        jsr     disp_at
        lda     aperr
        jsr     disp_hex
        jmp     seq

ok:     PUTS    5, 2, tssid
        lda     #5
        ldx     #8
        jsr     disp_at
        ldx     #AC_SSID
        ldy     #30
        jsr     disp_rpl

        PUTS    7, 2, tip
        lda     #7
        ldx     #8
        jsr     disp_at
        ldx     #AC_SIP
        ldy     #16
        jsr     disp_rpl

        PUTS    9, 2, tver
        lda     #9
        ldx     #8
        jsr     disp_at
        ldx     #AC_VER
        ldy     #15
        jsr     disp_rpl

        ; the sequence number, in hex
seq:    PUTS    12, 2, tseq
        lda     #12
        ldx     #10
        jsr     disp_at
        lda     FN_ACKSEQ
        jsr     disp_hex

        PUTS    14, 2, terr
        lda     #14
        ldx     #10
        jsr     disp_at
        lda     FN_ERR
        jsr     disp_hex

        jsr     disp_on
loop:   jmp     loop
.endproc

        .segment "RODATA"
title:  .byte "FUJINET ATARI 5200 M1", 0
tfail:  .byte "FAILED: E", 0
tssid:  .byte "SSID", 0
tip:    .byte "IP  ", 0
tver:   .byte "FW  ", 0
tseq:   .byte "ACKSEQ: ", 0
terr:   .byte "ERR:    ", 0

        .include "header.inc"
