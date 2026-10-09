; fujiboot.s -- M2: mount a cartridge image over the network and boot it.
;
; MOUNT_HOST -> SET_DEVICE_FULLPATH -> MOUNT_IMAGE. The last one makes the
; ESP32 side stream the .nes file back to the cartridge on the same link,
; addressed to the DBC device, while MOUNT_IMAGE's own reply is still
; outstanding; the cart stages it and reports BOOT_STATE. Then BOOTLOCK, and
; the loader ROM at $5800 copies the image into the SRAMs and jumps into it.
;
; Target host slot and path come from bootcfg.inc, written by build.sh:
;   BOOT_HOST=0 BOOT_PATH=/hello.nes ./build.sh fujiboot

        .include "fujinet.inc"
        .include "bootcfg.inc"

        .import disp_at, disp_str, disp_hex, disp_on, disp_dec
        .import fn_chk, fn_beg, fn_go, fn_ack, fn_pb, fn_path, fn_blk, fn_boot
        .importzp fn_ptr, fn_dev, fn_cmd, fn_npr
        .export main, nmi

DEVSLOT = 0

        .segment "ZEROPAGE"
aperr:  .res 1
apstep: .res 1
lastpct: .res 1

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
        sta     apstep
        PUTS    2, 2, title
        PUTS    4, 2, tpath
        lda     #4
        ldx     #8
        jsr     disp_at
        lda     #<bootpath
        sta     fn_ptr
        lda     #>bootpath
        sta     fn_ptr+1
        jsr     disp_str
        PUTS    6, 2, tmount
        jsr     disp_on

        jsr     fn_chk
        beq     have
        lda     #FNENOC
        sta     aperr
        jmp     show

; ---- MOUNT_HOST(host) ----
have:   lda     #FNDEVF
        sta     fn_dev
        lda     #FNCMHST
        sta     fn_cmd
        lda     #1
        sta     fn_npr
        jsr     fn_beg
        lda     #BOOTHST
        jsr     fn_pb
        lda     #1
        sta     apstep
        jsr     fn_go
        sta     aperr
        cmp     #FNEOK
        beq     :+
        jmp     show
:
        jsr     fn_ack
        sta     aperr
        cmp     #FNEOK
        beq     :+
        jmp     show
:

; ---- SET_DEVICE_FULLPATH(dev, host, mode, path) ----
; Three one-byte parameters and then a FULL 256-byte NUL-padded path.
        lda     #FNDEVF
        sta     fn_dev
        lda     #FNCSDFP
        sta     fn_cmd
        lda     #3
        sta     fn_npr
        jsr     fn_beg
        lda     #DEVSLOT
        jsr     fn_pb
        lda     #BOOTHST
        jsr     fn_pb
        lda     #FMREAD
        jsr     fn_pb
        lda     #<bootpath
        sta     fn_ptr
        lda     #>bootpath
        sta     fn_ptr+1
        jsr     fn_path
        lda     #2
        sta     apstep
        jsr     fn_go
        sta     aperr
        cmp     #FNEOK
        beq     :+
        jmp     show
:
        jsr     fn_ack
        sta     aperr
        cmp     #FNEOK
        beq     :+
        jmp     show
:

; ---- MOUNT_IMAGE(dev, mode) ----
; The one that takes real time: the ESP32 fetches the file and streams it to
; the cart as DBC frames while this reply is outstanding.
        lda     #FNDEVF
        sta     fn_dev
        lda     #FNCMIMG
        sta     fn_cmd
        lda     #2
        sta     fn_npr
        jsr     fn_beg
        lda     #DEVSLOT
        jsr     fn_pb
        lda     #FMREAD
        jsr     fn_pb
        lda     #3
        sta     apstep
        jsr     fn_go
        sta     aperr
        cmp     #FNEOK
        beq     :+
        jmp     show
:
        jsr     fn_ack
        sta     aperr
        cmp     #FNEOK
        beq     :+
        jmp     show
:

; ---- wait for the staged image ----
        lda     #4
        sta     apstep
        ldy     #0
wait1:  ldx     #0
wait2:  lda     FN_BST
        cmp     #FN_BRDY
        bne     :+
        jmp     boot
:       cmp     #FN_BFAIL
        bne     :+
        jmp     bfail
:
        dex
        bne     wait2
        dey
        bne     wait1
        lda     #FNEWAIT
        sta     aperr
        jmp     show
bfail:  lda     FN_BER
        sta     aperr
        lda     #5
        sta     apstep
        jmp     show

; ---- boot it ----
boot:   jsr     fn_blk          ; arm the load
        jmp     fn_boot         ; the loader ROM; does not return

; ---- the failure screen: WHICH step, with WHAT ----
show:   lda     #0
        sta     PPU_MASK
        PUTS    8, 2, tfail
        lda     #8
        ldx     #14
        jsr     disp_at
        lda     apstep
        jsr     disp_hex
        lda     #' '
        sta     PPU_DATA
        lda     #'E'
        sta     PPU_DATA
        lda     aperr
        jsr     disp_hex
        jsr     disp_on
hang:   jmp     hang
.endproc

.proc nmi
        rti
.endproc

        .segment "RODATA"
title:  .byte "FUJINET NES M2", 0
tpath:  .byte "BOOT: ", 0
tmount: .byte "MOUNTING...", 0
tfail:  .byte "FAILED STEP ", 0
bootpath: .byte BOOTPATH, 0
