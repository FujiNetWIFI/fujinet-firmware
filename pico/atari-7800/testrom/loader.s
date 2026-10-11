; loader.s -- the 512 bytes the cart serves at $0600: fills the SRAM and
; starts what it filled it with.
;
; Every client boots through here, and so does the power-on boot block.
; The cart moves into LOAD on our first write, then hands us the image one
; 1K slice at a time in the reply window, with the address to copy it to;
; we copy and ack. When it is all in, the cart says how to start it:
;
;   BIOS    the console's own BIOS, mapped back in, checks and starts the
;           game exactly as at power-on (INPTCTRL is still unlocked);
;   DIRECT  we start it, leaving what the BIOS leaves: X = S = $16, D and I
;           set, MARIA's DMA off.
;
; Either way the last instruction runs from a trampoline in zero page, so
; the arena can disappear under it. Nothing here returns.

        .include "fujinet.inc"

ptr     = $80                   ; where this slice goes
seq     = $82                   ; the last slice copied
silent  = $83                   ; the cart's silence, high byte
tramp   = $84                   ; 6 bytes

        .segment "LOADER"

        jmp     boot            ; $0600 FN_LOADER_BOOT
        jmp     config          ; $0603 FN_LOADER_CONFIG

config: sei
        cld
        sta     FN_REGSEL+FN_HOT_CONFIG
        jmp     start
boot:   sei
        cld
        sta     FN_REGSEL+FN_HOT_SWAP
start:  lda     #$7F            ; DMA off: no DLIs, and MARIA leaves the bus alone
        sta     CTRL
        ldx     #$FF
        txs
        lda     #0
        sta     seq
        sta     ptr             ; ptr..silent: how long the cart has been quiet
        sta     ptr+1
        sta     silent

        ; Yellow if the cart has not answered our first write in about two
        ; seconds: the console's writes below $1000 may not be reaching it.
first:  lda     FN_LOADSTATE
        bne     heard
        inc     ptr
        bne     first
        inc     ptr+1
        bne     first
        inc     silent
        lda     silent
        cmp     #4
        bne     first
        lda     #$1A
        sta     BACKGRND
        bne     first
heard:  lda     #0
        sta     BACKGRND

wait:   lda     FN_LOADSTATE
        cmp     #FN_LOAD_DONE
        beq     done
        cmp     #FN_LOAD_SLICE
        bne     wait
        lda     FN_LOADSEQ
        cmp     seq
        beq     wait
        sta     seq
        lda     FN_LOADDST
        sta     ptr+1
        ldy     #0
        sty     ptr
c0:     lda     FN_REPLY,y
        sta     (ptr),y
        iny
        bne     c0
        inc     ptr+1
c1:     lda     FN_REPLY+$100,y
        sta     (ptr),y
        iny
        bne     c1
        inc     ptr+1
c2:     lda     FN_REPLY+$200,y
        sta     (ptr),y
        iny
        bne     c2
        inc     ptr+1
c3:     lda     FN_REPLY+$300,y
        sta     (ptr),y
        iny
        bne     c3
        lda     seq
        sta     FN_REGSEL+FN_REG_SLICE_ACK
        jmp     wait

done:   lda     FN_HANDOVER
        cmp     #FN_HO_BIOS
        beq     viabios

        ldx     #5              ; DIRECT: STA GO / JMP ($FFFC)
@d:     lda     tr_direct,x
        sta     tramp,x
        dex
        bpl     @d
        ldx     #$16
        txs
        sed
        jmp     tramp

viabios:
        lda     #0              ; the joystick ports as the BIOS expects them
        sta     CTLSWA
        sta     CTLSWB
        sta     FN_REGSEL+FN_HOT_GO_BIOS
        ldx     #5              ; STA INPTCTRL / JMP ($FFFC)
@b:     lda     tr_bios,x
        sta     tramp,x
        dex
        bpl     @b
        lda     #$02            ; BIOS mapped in, MARIA on, unlocked
        jmp     tramp

        .segment "LDATA"
tr_direct:
        .byte   $8D, <(FN_REGSEL+FN_HOT_GO), >(FN_REGSEL+FN_HOT_GO), $6C, $FC, $FF
tr_bios:
        .byte   $8D, $01, $00, $6C, $FC, $FF
