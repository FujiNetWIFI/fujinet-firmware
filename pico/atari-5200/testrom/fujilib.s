; fujilib.s -- the 6502 side of the FujiNet cartridge mailbox: the code.
;
; The 5200 edge has no R/W line, so every byte reaches the cart in the
; address of a read: a register write is `lda FN_REGSEL+n` then
; `lda FN_REGDATA,y` with the value in Y. Both bases are page-aligned, so
; an index never carries into another page.

        .include "fujinet.inc"

        .export fn_chk, fn_rw, fn_beg, fn_txb, fn_pb, fn_pw, fn_path
        .export fn_go, fn_ack, fn_blk, fn_boot, fn_config, fn_frames, fn_wait

        .segment "CODE"

; fn_rw -- register X = A. Clobbers A, Y.
.proc fn_rw
        tay
        lda     FN_REGSEL,x
        lda     FN_REGDATA,y
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
        lda     #12             ; seconds: past the cart's 5 s window for ordinary commands
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

; fn_txb -- append A to the TX stream. Clobbers Y.
.proc fn_txb
        tay
        lda     FN_TXDATA,y
        rts
.endproc

; fn_pb -- append a one-byte parameter with value A.
.proc fn_pb
        pha
        ldy     #1
        lda     FN_TXDATA,y
        pla
        tay
        lda     FN_TXDATA,y
        rts
.endproc

; fn_pw -- append a two-byte little-endian parameter. A = low, X = high.
.proc fn_pw
        pha
        ldy     #2
        lda     FN_TXDATA,y
        pla
        tay
        lda     FN_TXDATA,y
        txa
        tay
        lda     FN_TXDATA,y
        rts
.endproc

; fn_path -- append the NUL-terminated string at FN_PTR to the TX stream,
; padded with NULs to EXACTLY 256 bytes: OPEN_DIRECTORY and
; SET_DEVICE_FULLPATH both read exactly 256.
.proc fn_path
        ldy     #0
loop:   lda     (FN_PTR),y
        beq     pad
        tax
        lda     FN_TXDATA,x
        iny
        bne     loop
        rts
pad:    lda     FN_TXDATA
        iny
        bne     pad
        rts
.endproc

; fn_wait -- count down FN_CNT by the frames that have passed. C clear once
; it runs out.
.proc fn_wait
        lda     RTCLOKL
        cmp     FN_LAST
        beq     still
        sta     FN_LAST
        lda     FN_CNT
        bne     :+
        dec     FN_CNT+1
:       dec     FN_CNT
        lda     FN_CNT
        ora     FN_CNT+1
        bne     still
        clc
        rts
still:  sec
        rts
.endproc

; fn_frames -- FN_CNT = A seconds of frames (x 64, near enough to 60).
.proc fn_frames
        sta     FN_CNT+1
        lda     #0
        lsr     FN_CNT+1
        ror     a
        lsr     FN_CNT+1
        ror     a
        sta     FN_CNT
        lda     RTCLOKL
        sta     FN_LAST
        rts
.endproc

; fn_go -- commit and wait. Returns A = FNE* (0 = the transaction completed).
; The next sequence number comes from the CART's ACKSEQ + 1, never a counter
; in RAM: a power cycle restarts the client, not the cart.
.proc fn_go
        lda     FN_ACKSEQ
        clc
        adc     #1
        bne     have
        lda     #1              ; 0 is reserved as "never used"
have:   sta     FN_SEQ
        ldx     #FN_REG_SEQ
        jsr     fn_rw           ; launching it is this one pair
        lda     FN_TMO
        jsr     fn_frames
wait:   lda     FN_ACKSEQ       ; published LAST, after the whole reply
        cmp     FN_SEQ
        beq     got
        jsr     fn_wait
        bcs     wait
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

; fn_blk -- arm the staged image (FN_REG_BOOTLOCK).
.proc fn_blk
        lda     #FN_BOOTLOCK_MAGIC
        ldx     #FN_REG_BOOTLOCK
        jmp     fn_rw
.endproc

; fn_boot -- arm the staged image and hand the console to it. Returns only
; if the cart never armed the swap, with A = FNENARM.
.proc fn_boot
        jsr     fn_blk
        jmp     handover
.endproc

; fn_config -- back to CONFIG. Returns only if the cart never armed it.
.proc fn_config
        lda     #FN_CONFIG_MAGIC
        ldx     #FN_REG_CONFIG
        jsr     fn_rw
        jmp     handover
.endproc

; Wait for FN_ARMED, quiet the machine as a power-on leaves it, and jump to
; the cart's stub: its JMP ($FFFC) is the last cart read before the BIOS's
; reset code, and the read of its last byte swaps the image in.
.proc handover
        lda     #2
        jsr     fn_frames
wait:   lda     FN_ARMED
        bne     armed
        jsr     fn_wait
        bcs     wait
        lda     #FNENARM
        rts
armed:  sei
        lda     #0
        sta     NMIEN           ; first: a pending NMI would run through $0202
        sta     NMIRES
        sta     DMACTL          ; no display-list fetch may reach the new image
        sta     GRACTL
        sta     IRQEN
vbl:    lda     VCOUNT          ; let the frame's DMA finish
        cmp     #124
        bcc     vbl
        lda     #0
        tax
chips:  sta     POKEY,x
        sta     GTIA,x
        sta     ANTIC,x
        inx
        bne     chips
ramlo:  .repeat 32, I              ; two halves: one is past a branch's reach
        sta     I * $100,x
        .endrepeat
        inx
        bne     ramlo
ramhi:  .repeat 32, I
        sta     $2000 + I * $100,x
        .endrepeat
        inx
        bne     ramhi
        clc                     ; the flags as the BIOS's own start leaves them
        jmp     FN_STUB
.endproc
