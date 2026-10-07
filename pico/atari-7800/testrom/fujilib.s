; fujilib.s -- the 6502 side of the FujiNet cartridge mailbox: the code.
;
; A register write is ONE store: `sta FN_REGSEL,x` with the register in X and
; the value in A. The base's low byte is $00, so the index never carries and
; the store's extra cycle -- a READ of the same address -- is inert here.

        .include "fujinet.inc"

        .export fn_chk, fn_rw, fn_beg, fn_txb, fn_pb, fn_pw, fn_path
        .export fn_go, fn_ack, fn_blk, fn_boot

        .segment "CODE"

; fn_rw -- register X = A.
.proc fn_rw
        sta     FN_REGSEL,x
        rts
.endproc

; fn_chk -- Z set if a FujiNet cartridge is answering.
.proc fn_chk
        lda     FN_MAGIC
        cmp     #'F'
        bne     done
        lda     FN_MAGIC+1
        cmp     #'N'
done:   rts
.endproc

; fn_beg -- begin the transaction described by FN_DEV / FN_CMD / FN_NPR.
; Rewinds the TX stream last, so parameters may be appended at once. Resets
; FN_TMO, so a slow command raises it after this.
.proc fn_beg
        lda     #16             ; ~11 s: past the cart's 5 s window for ordinary commands
        sta     FN_TMO
        lda     FN_DEV
        ldx     #FN_REG_DEVICE
        jsr     fn_rw
        lda     FN_CMD
        ldx     #FN_REG_CMD
        jsr     fn_rw
        lda     FN_NPR
        ldx     #FN_REG_NPARAM
        jsr     fn_rw
        lda     #0
        ldx     #FN_REG_DATA_RST
        jmp     fn_rw
.endproc

; fn_txb -- append A to the TX stream.
.proc fn_txb
        sta     FN_TXDATA
        rts
.endproc

; fn_pb -- append a one-byte parameter with value A.
.proc fn_pb
        pha
        lda     #1
        sta     FN_TXDATA
        pla
        sta     FN_TXDATA
        rts
.endproc

; fn_pw -- append a two-byte little-endian parameter. A = low, X = high.
.proc fn_pw
        pha
        lda     #2
        sta     FN_TXDATA
        pla
        sta     FN_TXDATA
        txa
        sta     FN_TXDATA
        rts
.endproc

; fn_path -- append the NUL-terminated string at FN_PTR to the TX stream,
; padded with NULs to EXACTLY 256 bytes: OPEN_DIRECTORY and
; SET_DEVICE_FULLPATH both read exactly 256.
.proc fn_path
        ldy     #0
loop:   lda     (FN_PTR),y
        beq     pad
        sta     FN_TXDATA
        iny
        bne     loop
        rts
pad:    lda     #0
padl:   sta     FN_TXDATA
        iny
        bne     padl
        rts
.endproc

; fn_go -- commit and wait. Returns A = FNE* (0 = the transaction completed).
; The next sequence number comes from the CART's persisted ACKSEQ + 1, never
; a counter in RAM: a power cycle restarts the client, not the cart.
.proc fn_go
        lda     FN_ACKSEQ
        clc
        adc     #1
        bne     have
        lda     #1              ; 0 is reserved as "never used"
have:   sta     FN_SEQ
        ldx     #FN_REG_SEQ
        jsr     fn_rw           ; launching it is this single store
        lda     FN_TMO
        sta     FN_CNT
outer:  ldy     #0
mid:    ldx     #0
inner:  lda     FN_ACKSEQ       ; published LAST, after the whole reply
        cmp     FN_SEQ
        beq     got
        dex
        bne     inner
        dey
        bne     mid
        dec     FN_CNT
        bne     outer
        lda     #FNEWAIT
        rts
got:    lda     FN_ERR
        rts
.endproc

; fn_ack -- 0 on success, FNENAK if the server said no. Call after fn_go
; returned 0.
.proc fn_ack
        lda     FN_REPLYCMD
        cmp     #$06
        beq     ok
        lda     #FNENAK
        rts
ok:     lda     #FNEOK
        rts
.endproc

; fn_blk -- arm the image load (FN_REG_BOOTLOCK).
.proc fn_blk
        lda     #FN_BOOTLOCK_MAGIC
        ldx     #FN_REG_BOOTLOCK
        jmp     fn_rw
.endproc

; fn_boot -- hand the console to the loader. Does not return.
.proc fn_boot
        sei
        lda     #$7F
        sta     CTRL
        jmp     FN_LOADER_BOOT
.endproc
