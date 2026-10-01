; loader.s -- the 2K loader ROM the cart serves at $5800-$5FFF.
;
; This is the swap stub for every image, CONFIG at power-on included. It is
; cart-served, so unlike every sibling port it survives the copy it performs
; and nothing has to be moved into console RAM first.
;
; Protocol (fuji_mailbox.h): store to FN_RSEL+FH_SWAP to ask for the load,
; then for every LSEQ bump copy the 1K in the reply window to
;   PRG: $8000 + LOFF*1K   (the cart has moved PRG slot 0 under it)
;   CHR: PPU LOFF*1K       (the cart has moved the CHR window under it)
; and ack with FR_SACK. On LDONE, ack once more and JMP ($FFFC).
;
; Layout the cart relies on: JMP at $5800, RTI at $5803 (its NMI/IRQ vectors
; while the SRAM is off point there).
;
; 9.5 cycles a byte: a 32K CONFIG lands in 0.17 s, a 512K+256K game in ~4 s.

        .include "fujinet.inc"

        .segment "ZEROPAGE"
jmpv:   .res 2                  ; $00-$01: the copy routine to run
lseq:   .res 1                  ; $02: the last slice sequence we copied

        .segment "LOADER"

entry:  jmp     start           ; $5800
        rti                     ; $5803: NMI and IRQ land here

start:  sei
        cld
        ldx     #$FF
        txs
        lda     #0
        sta     PPU_CTRL        ; NMI off
        sta     PPU_MASK        ; rendering off: the CHR copy needs the PPU idle
        sta     APU_STATUS
        sta     lseq

        ; Two vblanks: at power-on the PPU is not ready before that, and a
        ; PPU_MASK write only takes effect from the next frame anyway.
        bit     PPU_STATUS
vbl1:   bit     PPU_STATUS
        bpl     vbl1
vbl2:   bit     PPU_STATUS
        bpl     vbl2

        sta     FN_RSEL+FH_SWAP ; the request; at power-on the cart is already loading

loop:   lda     FN_LSTATE
        cmp     #FN_LDONE
        bne     :+
        jmp     done
:       cmp     #FN_LFAIL
        bne     :+
        jmp     fail
:       cmp     #FN_LSLICE
        bne     loop
        lda     FN_LSEQ
        cmp     lseq
        beq     loop
        sta     lseq
        lda     FN_LOFF
        cmp     #8
        bcs     loop            ; not a value the cart publishes: a missed cycle
        ldy     FN_LDST
        beq     prg
        cpy     #1
        bne     loop
        jmp     chr

; ---- PRG: 1K to $8000 + LOFF * $400, through one of eight copy loops ----
prg:    asl     a
        tay
        lda     prgtab,y
        sta     jmpv
        lda     prgtab+1,y
        sta     jmpv+1
        jmp     (jmpv)

.macro  COPY1K  dst
        ldx     #0
:       lda     FN_RPLY,x
        sta     dst,x
        lda     FN_RPLY+$100,x
        sta     dst+$100,x
        lda     FN_RPLY+$200,x
        sta     dst+$200,x
        lda     FN_RPLY+$300,x
        sta     dst+$300,x
        inx
        bne     :-
        jmp     ack
.endmacro

copy0:  COPY1K  $8000
copy1:  COPY1K  $8400
copy2:  COPY1K  $8800
copy3:  COPY1K  $8C00
copy4:  COPY1K  $9000
copy5:  COPY1K  $9400
copy6:  COPY1K  $9800
copy7:  COPY1K  $9C00

prgtab: .addr   copy0, copy1, copy2, copy3, copy4, copy5, copy6, copy7

; ---- CHR: 1K through PPU_DATA, address LOFF * $400 ----
; PPU_DATA auto-increments, so the bytes must go in address order: one page
; at a time, four bytes per iteration within the page.
.macro  CHRPAGE src
        ldx     #0
:       lda     src,x
        sta     PPU_DATA
        lda     src+1,x
        sta     PPU_DATA
        lda     src+2,x
        sta     PPU_DATA
        lda     src+3,x
        sta     PPU_DATA
        inx
        inx
        inx
        inx
        bne     :-
.endmacro

chr:    asl     a
        asl     a
        sta     PPU_ADDR        ; high byte: LOFF << 2
        lda     #0
        sta     PPU_ADDR
        CHRPAGE FN_RPLY
        CHRPAGE FN_RPLY+$100
        CHRPAGE FN_RPLY+$200
        CHRPAGE FN_RPLY+$300
        jmp     ack

ack:    lda     lseq
        sta     FN_RSEL+FR_SACK
        jmp     loop

; ---- into the new image, through its own reset vector ----
done:   lda     lseq
        sta     FN_RSEL+FR_SACK ; the goodbye: the cart may now drop the mailbox
        ldx     #$FF
        txs
        jmp     ($FFFC)

; A failed load leaves a dark screen. The console's Reset re-enters the
; image's vectors, which the SRAM now holds half-written; power-cycling brings
; CONFIG back.
fail:   jmp     fail
