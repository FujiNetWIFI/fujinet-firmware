; fujitest.s -- GET_ADAPTERCONFIG_EXTENDED: SSID, IP and firmware version on
; screen, plus ACKSEQ for emu/resettest.lua. Runs with the screen dark.

        .include "fujinet.inc"

        .import disp_at, disp_str, disp_hex, disp_rpl, disp_on, disp_putc
        .import fn_chk, fn_beg, fn_go, fn_ack
        .importzp fn_ptr, fn_dev, fn_cmd, fn_npr
        .export main, nmi

; Field offsets in the GET_ADAPTERCONFIG_EXTENDED reply, all below 256.
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
        sta     fn_ptr
        lda     #>str
        sta     fn_ptr+1
        jsr     disp_str
.endmacro

.proc main
        lda     #0
        sta     aperr
        jsr     fn_chk
        beq     have
        lda     #FNENOC
        sta     aperr
        jmp     show

have:   lda     #FNDEVF
        sta     fn_dev
        lda     #FNCADPX
        sta     fn_cmd
        lda     #0
        sta     fn_npr
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
        ldx     #6
        jsr     disp_at
        ldx     #AC_SSID
        ldy     #24
        jsr     disp_rpl

        PUTS    7, 2, tip
        lda     #7
        ldx     #6
        jsr     disp_at
        ldx     #AC_SIP
        ldy     #16
        jsr     disp_rpl

        PUTS    9, 2, tver
        lda     #9
        ldx     #6
        jsr     disp_at
        ldx     #AC_VER
        ldy     #15
        jsr     disp_rpl

seq:    PUTS    12, 2, tseq
        lda     #12
        ldx     #10
        jsr     disp_at
        lda     FN_ACKS
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

.proc nmi
        rti
.endproc

        .segment "RODATA"
title:  .byte "FUJINET NES M1", 0
tfail:  .byte "FAILED: E", 0
tssid:  .byte "SSID", 0
tip:    .byte "IP  ", 0
tver:   .byte "FW  ", 0
tseq:   .byte "ACKSEQ: ", 0
terr:   .byte "ERR:    ", 0
