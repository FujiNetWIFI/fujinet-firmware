; fujibank.s -- M2: a FujiNet app bigger than the slot: SuperGame banking with
; the mailbox live, then it boots a game itself.
;
; Each of three 16K banks, switched into $8000, runs a real transaction from
; its own code and names itself on screen; then the app pushes and boots
; BOOT_PATH as CONFIG would. The claim in the fixed bank says "SuperGame with
; cart RAM" (kind byte = A78MAP_SG_RAM + 1).

        .include "fujinet.inc"
        .include "bootcfg.inc"

        .import disp_at, disp_str, disp_hex, disp_on, disp_rpl, disp_vbl
        .import fn_chk, fn_beg, fn_go, fn_ack, fn_pb, fn_path, fn_blk, fn_boot
        .export main, nmi

AC_SSID = 0

        .segment "ZEROPAGE"
bank:   .res 1
aperr:  .res 1
apstep: .res 1

; In each bank, at $8000: A = the bank's number on return, and the
; transaction's result in aperr.
.macro  BANKCODE n
        .segment .sprintf("BANK%d", n)
        lda     #FNDEVF
        sta     FN_DEV
        lda     #FNCADPX
        sta     FN_CMD
        lda     #0
        sta     FN_NPR
        jsr     fn_beg
        jsr     fn_go
        sta     aperr
        lda     #n
        rts
.endmacro

        BANKCODE 0
        BANKCODE 1
        BANKCODE 2

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
        PUTS    2, 2, title
        jsr     disp_on
        lda     #0
        sta     bank
@b:     lda     bank
        sta     $8000           ; SuperGame: any write to $8000-$BFFF picks the bank
        jsr     $8000           ; the transaction, from the bank's own code
        cmp     bank
        beq     @right
        jmp     wrong
@right: pha
        lda     bank
        asl
        clc
        adc     #5
        ldx     #2
        jsr     disp_at
        lda     #<tbank
        sta     FN_PTR
        lda     #>tbank
        sta     FN_PTR+1
        jsr     disp_str
        pla
        jsr     disp_hex
        lda     #' '
        jsr     disp_putc
        lda     aperr
        jsr     disp_hex
        lda     #' '
        jsr     disp_putc
        ldx     #AC_SSID
        ldy     #20
        jsr     disp_rpl
        inc     bank
        lda     bank
        cmp     #3
        beq     @pushed
        jmp     @b
@pushed:
        ldx     #120            ; two seconds to read the banks
@pause: jsr     disp_vbl
        dex
        bne     @pause

        ; push the game and boot it, as CONFIG does
        PUTS    12, 2, tboot
        lda     #FNDEVF
        sta     FN_DEV
        lda     #FNCMHST
        sta     FN_CMD
        lda     #1
        sta     FN_NPR
        jsr     fn_beg
        lda     #BOOTHST
        jsr     fn_pb
        lda     #1
        jsr     go_ack
        lda     #FNDEVF
        sta     FN_DEV
        lda     #FNCSDFP
        sta     FN_CMD
        lda     #3
        sta     FN_NPR
        jsr     fn_beg
        lda     #0
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
        lda     #2
        jsr     go_ack
        lda     #FNDEVF
        sta     FN_DEV
        lda     #FNCMIMG
        sta     FN_CMD
        lda     #2
        sta     FN_NPR
        jsr     fn_beg
        lda     #0
        jsr     fn_pb
        lda     #FMREAD
        jsr     fn_pb
        lda     #96             ; ~67 s: past the cart's 60 s for MOUNT_IMAGE
        sta     FN_TMO
        lda     #3
        jsr     go_ack

        lda     #4
        sta     apstep
        ldy     #0
@w1:    ldx     #0
@w2:    lda     FN_BOOTSTATE
        cmp     #FN_BOOT_READY
        beq     @boot
        cmp     #FN_BOOT_FAILED
        beq     @bfail
        dex
        bne     @w2
        dey
        bne     @w1
        lda     #FNEWAIT
        sta     aperr
        jmp     fail
@bfail: lda     FN_BOOTERR
        sta     aperr
        lda     #5
        sta     apstep
        jmp     fail
@boot:  jsr     fn_blk
        jmp     fn_boot

wrong:  PUTS    12, 2, twrong
@h:     jmp     @h
.endproc

; go_ack -- fn_go and fn_ack as step A; the failure screen if either fails.
.proc go_ack
        sta     apstep
        jsr     fn_go
        sta     aperr
        cmp     #FNEOK
        bne     bad
        jsr     fn_ack
        sta     aperr
        cmp     #FNEOK
        bne     bad
        rts
bad:    jmp     fail
.endproc

; fail -- WHICH step, with WHAT. Does not return.
.proc fail
        PUTS    14, 2, tfail
        lda     #14
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

        .import disp_putc

.proc nmi
        rti
.endproc

        .segment "RODATA"
title:  .byte "FUJINET ATARI 7800 M2: BANKED APP", 0
tbank:  .byte "BANK ", 0
tboot:  .byte "BOOTING...", 0
twrong: .byte "WRONG BANK ANSWERED", 0
tfail:  .byte "FAILED STEP ", 0
bootpath: .byte BOOTPATH, 0
