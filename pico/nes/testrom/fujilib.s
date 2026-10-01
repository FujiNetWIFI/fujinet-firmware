; fujilib.s -- the 6502 side of the FujiNet cartridge mailbox: the code.
;
; A register write is ONE store: `sta FN_RSEL,x` with the register in X and
; the value in A. The base's low byte is $00, so the index never carries and
; the store's extra cycle -- a READ of the same address -- is inert here.

        .include "fujinet.inc"

        .export fn_chk, fn_rw, fn_beg, fn_txb, fn_pb, fn_pw, fn_path
        .export fn_go, fn_ack, fn_blk, fn_boot
        .exportzp fn_dev, fn_cmd, fn_npr, fn_ptr, fn_tmo

fn_dev  = FN_DEV
fn_cmd  = FN_CMD
fn_npr  = FN_NPR
fn_ptr  = FN_PTR
fn_tmo  = FN_TMO

        .segment "CODE"

; fn_rw -- register X = A.
.proc fn_rw
        sta     FN_RSEL,x
        rts
.endproc

; fn_chk -- Z set if a FujiNet cartridge is answering.
.proc fn_chk
        lda     FN_MAG0
        cmp     #'F'
        bne     done
        lda     FN_MAG1
        cmp     #'N'
done:   rts
.endproc

; fn_beg -- begin the transaction described by fn_dev / fn_cmd / fn_npr.
; Rewinds the TX stream last, so parameters may be appended at once.
.proc fn_beg
        lda     #16             ; ~11 s: longer than the cart's own 5 s budget
        sta     FN_TMO          ;   so a real timeout surfaces as ITS error
        lda     FN_DEV
        ldx     #FR_DEV
        jsr     fn_rw
        lda     FN_CMD
        ldx     #FR_CMD
        jsr     fn_rw
        lda     FN_NPR
        ldx     #FR_NPAR
        jsr     fn_rw
        lda     #0
        ldx     #FR_DRST
        jmp     fn_rw
.endproc

; fn_txb -- append A to the TX stream.
.proc fn_txb
        sta     FN_TX
        rts
.endproc

; fn_pb -- append a one-byte parameter with value A.
.proc fn_pb
        pha
        lda     #1
        sta     FN_TX
        pla
        sta     FN_TX
        rts
.endproc

; fn_pw -- append a two-byte little-endian parameter. A = low, X = high.
; SET_DIRECTORY_POSITION takes ONE parameter of TWO bytes, not two of one.
.proc fn_pw
        pha
        lda     #2
        sta     FN_TX
        pla
        sta     FN_TX
        txa
        sta     FN_TX
        rts
.endproc

; fn_path -- append the NUL-terminated string at fn_ptr to the TX stream,
; padded with NULs to EXACTLY 256 bytes. OPEN_DIRECTORY and
; SET_DEVICE_FULLPATH both read exactly 256 and fail a short payload.
.proc fn_path
        ldy     #0
loop:   lda     (fn_ptr),y
        beq     pad
        sta     FN_TX
        iny
        bne     loop
        rts                     ; exactly 256 bytes: no padding needed
pad:    lda     #0
padl:   sta     FN_TX           ; Y wraps to 0 after 256 - Y stores
        iny
        bne     padl
        rts
.endproc

; fn_go -- commit and wait. Returns A = FNE* (0 = the transaction completed).
;
; THE RULE EVERY PORT LEARNED THE HARD WAY: the next sequence number comes
; from the CART'S OWN persisted ACKSEQ + 1, never from a counter in RAM. A
; console RESET restarts the client and re-zeroes its variables but does NOT
; reset the cartridge -- there is no reset line on the edge.
.proc fn_go
        lda     FN_ACKS
        clc
        adc     #1
        bne     have
        lda     #1              ; 0 is reserved as "never used"
have:   sta     FN_SEQ
        ldx     #FR_SEQ
        jsr     fn_rw           ; launching it is this single store
        lda     FN_TMO
        sta     FN_CNT
outer:  ldy     #0
mid:    ldx     #0
        ; ACKSEQ is published LAST, after the whole reply is in place.
inner:  lda     FN_ACKS
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
        lda     FN_RCMD
        cmp     #$06
        beq     ok
        lda     #FNENAK
        rts
ok:     lda     #FNEOK
        rts
.endproc

; fn_blk -- arm the image load (FN_REG_BOOTLOCK).
.proc fn_blk
        lda     #FN_BLKM
        ldx     #FR_BLCK
        jmp     fn_rw
.endproc

; fn_boot -- hand the machine to the loader ROM. Does not return. The loader
; is cart-served and untouched by the SRAM copy, so nothing is copied into
; console RAM first; it just has to run with NMI off and the screen dark.
.proc fn_boot
        sei
        lda     #0
        sta     PPU_CTRL
        sta     PPU_MASK
        sta     APU_STATUS
        jmp     FN_LOADER
.endproc
