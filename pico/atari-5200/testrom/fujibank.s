; fujibank.s -- M2: a 64K Super Cart FujiNet app. Bank 1 (power-on) runs a
; transaction, calls into bank 0 -- whose own code runs another one through
; the common fujilib -- and comes back; then it mounts and boots a game like
; fujiboot. The arena is served in every bank.
;
; Banks switch on reads: $BFD0 selects bank 0, $BFE0-$BFFF the last bank.

        .macpack longbranch
        .include "fujinet.inc"
        .include "bootcfg.inc"

        .import disp_init, disp_at, disp_str, disp_hex, disp_rpl, disp_on, disp_putc
        .import fn_chk, fn_beg, fn_go, fn_ack, fn_pb, fn_path, fn_boot
        .import fn_frames, fn_wait
        .export main, far0

BANK0_ENTRY = $A000
DEVSLOT = 0
AC_SIP  = 140

        .segment "ZEROPAGE"
aperr:  .res 1
apstep: .res 1

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

; far0 -- call bank 0's entry and come back to bank 1. Common code: the
; same bytes are at this address in both banks.
.proc far0
        bit     $BFD0           ; bank 0 from the next fetch on
        jsr     BANK0_ENTRY
        bit     $BFE0           ; the last bank again; A is bank 0's answer
        rts
.endproc

.proc main
        jsr     disp_init
        lda     #0
        sta     aperr
        sta     apstep
        PUTS    2, 2, title
        PUTS    4, 2, tb1
        lda     #4
        ldx     #9
        jsr     disp_at
        lda     marker
        jsr     disp_putc
        jsr     disp_on
        jsr     fn_chk
        beq     :+
        lda     #FNENOC
        sta     aperr
        jmp     show
:       jsr     adapter
        jne     show
        PUTS    5, 2, tip
        lda     #5
        ldx     #9
        jsr     disp_at
        ldx     #AC_SIP
        ldy     #16
        jsr     disp_rpl

        lda     #1
        sta     apstep
        jsr     far0            ; bank 0 runs a transaction of its own
        sta     aperr
        cmp     #FNEOK
        beq     :+
        jmp     show
:       PUTS    8, 2, tback
        lda     #8
        ldx     #22
        jsr     disp_at
        lda     marker          ; bank 1's byte again: the switch came back
        jsr     disp_putc

; ---- MOUNT_HOST, SET_DEVICE_FULLPATH, MOUNT_IMAGE, then boot ----
        lda     #2
        sta     apstep
        lda     #FNCMHST
        sta     FN_CMD
        lda     #1
        jsr     begin
        lda     #BOOTHST
        jsr     fn_pb
        jsr     go
        jne     show
        lda     #3
        sta     apstep
        lda     #FNCSDFP
        sta     FN_CMD
        lda     #3
        jsr     begin
        lda     #DEVSLOT
        jsr     fn_pb
        lda     #BOOTHST
        jsr     fn_pb
        lda     #FMREAD
        jsr     fn_pb
        lda     #<bootpath
        sta     FN_PTR
        lda     #>bootpath
        sta     FN_PTR+1
        jsr     fn_path
        jsr     go
        jne     show
        lda     #4
        sta     apstep
        lda     #FNCMIMG
        sta     FN_CMD
        lda     #2
        jsr     begin
        lda     #DEVSLOT
        jsr     fn_pb
        lda     #FMREAD
        jsr     fn_pb
        lda     #66
        sta     FN_TMO
        jsr     go
        jne     show
        lda     #5
        sta     apstep
        lda     #10
        jsr     fn_frames
wait:   lda     FN_BOOTSTATE
        cmp     #FN_BOOT_READY
        beq     boot
        cmp     #FN_BOOT_FAILED
        beq     bfail
        jsr     fn_wait
        bcs     wait
        lda     #FNEWAIT
        sta     aperr
        jmp     show
bfail:  lda     FN_BOOTERR
        sta     aperr
        jmp     show
boot:   jsr     fn_boot
        sta     aperr
        lda     #6
        sta     apstep

show:   PUTS    12, 2, tfail
        lda     #12
        ldx     #14
        jsr     disp_at
        lda     apstep
        jsr     disp_hex
        lda     #' '
        jsr     disp_putc
        lda     #'E'
        jsr     disp_putc
        lda     aperr
        jsr     disp_hex
hang:   jmp     hang
.endproc

; begin -- FUJI device, command FN_CMD, A parameters.
.proc begin
        sta     FN_NPR
        lda     #FNDEVF
        sta     FN_DEV
        jmp     fn_beg
.endproc

; go -- commit; Z set and aperr 0 when the server said yes.
.proc go
        jsr     fn_go
        sta     aperr
        cmp     #FNEOK
        bne     done
        jsr     fn_ack
        sta     aperr
        cmp     #FNEOK
done:   rts
.endproc

; adapter -- GET_ADAPTERCONFIG_EXTENDED; Z set on success.
.proc adapter
        lda     #FNCADPX
        sta     FN_CMD
        lda     #0
        jsr     begin
        jmp     go
.endproc
        .export adapter

        .segment "RODATA"
title:  .byte "FUJINET ATARI 5200 M2 BANKED", 0
tb1:    .byte "BANK 1", 0
tip:    .byte "IP", 0
tback:  .byte "BACK FROM BANK 0: BANK", 0
tfail:  .byte "FAILED STEP ", 0
bootpath: .byte BOOTPATH, 0

        .segment "BANKCODE"
        .res    3               ; bank 0's entry jump
marker: .byte "1"               ; $A003: bank 0 has '0' here

        .segment "CLAIM"
        .byte   "FUJI"
        .byte   1               ; claim version
        .byte   4               ; mapper: A52MAP_SUPERCART + 1
        .byte   0
        .byte   $02             ; $BFE7: PAL-compatible

        .segment "HEADER"
        .byte   "FUJINET             "
        .byte   $FF, $FF
        .import start
        .word   start
