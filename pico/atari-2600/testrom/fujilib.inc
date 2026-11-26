; fujilib.inc -- the 6502 side of the FujiNet cartridge mailbox. Include it inside
; the ORG, after fujinet.inc.
;
; A register write is an arm (sta FNRSEL+reg, data ignored) then a commit
; (sta FNCMT): with no R/W line a stray read decodes as a store of the floating
; bus, and one stray alone changes nothing. Only STA/STX/STY on the control and
; TX pages: an RMW's write burst has no defined sample (checkrom.py enforces).

; -----------------------------------------------------------------------
; FNRW -- register X = A. The whole transport is built on this.
;
; `sta FNRSEL,x`: the base low byte is $00, so the index never carries and the
; dummy read lands on the write's own address, one parked access.
FNRW:   sta     FNRSEL,x
        sta     FNCMT
        rts

; FNARM -- open the decode gate.
; Two stores of two specific values, which a 7800 BIOS probe cannot produce.
FNARM:  lda     #FNAM1
        sta     FNRSEL+FH_ARM1
        lda     #FNAM2
        sta     FNRSEL+FH_ARM2
        rts

; FNCHK -- Z set if a FujiNet cartridge is answering.
FNCHK:  lda     FNMAG0
        cmp     #'F'
        bne     FNCHK1
        lda     FNMAG1
        cmp     #'N'
FNCHK1: rts

; FNBEG -- begin a transaction described by FNDEV / FNCMD / FNNPR.
; Rewinds the TX stream last, so parameters may be appended immediately.
FNBEG:  lda     #16             ; ~9s; longer than the cart's own 5s budget so
        sta     FNTMO           ;   a real timeout is reported as ITS error
        lda     FNDEV
        ldx     #FR_DEV
        jsr     FNRW
        lda     FNCMD
        ldx     #FR_CMD
        jsr     FNRW
        lda     FNNPR
        ldx     #FR_NPAR
        jsr     FNRW
        lda     #0
        ldx     #FR_DRST
        jsr     FNRW
        rts

; FNTXB -- append A to the TX stream.
FNTXB:  sta     FNTX
        rts

; FNPB -- append a one-byte parameter with value A.
; The stream is NPARAM x { size byte, value bytes }, then the payload.
FNPB:   pha
        lda     #1
        sta     FNTX
        pla
        sta     FNTX
        rts

; FNPW -- append a two-byte little-endian parameter. A = low, X = high.
; SET_DIRECTORY_POSITION takes one two-byte parameter (a uint16_t param(0)),
; not two one-byte ones; that mistake NAKs.
FNPW:   pha
        lda     #2
        sta     FNTX
        pla
        sta     FNTX
        txa
        sta     FNTX
        rts

; FNGO -- commit and wait. Returns A = FNE* (0 = the transaction completed).
; The next sequence is the cart's ACKSEQ + 1, never a RAM counter: a console
; RESET restarts the client but not the cart, and a repeated sequence is dropped.
FNGO:   lda     FNACKS
        clc
        adc     #1
        bne     FNGO1
        lda     #1              ; 0 is reserved as "never used"
FNGO1:  sta     FNSEQ
        ldx     #FR_SEQ
        jsr     FNRW            ; launching it is this single commit
        lda     FNTMO
        sta     FNCNT
FNGO2:  ldy     #0
FNGO3:  ldx     #0
        ; ACKSEQ is published last, so a match means the reply is in place.
FNGO4:  lda     FNACKS
        cmp     FNSEQ
        beq     FNGO5
        dex
        bne     FNGO4
        dey
        bne     FNGO3
        dec     FNCNT
        bne     FNGO2
        lda     #FNEWAIT
        rts
FNGO5:  lda     FNERR
        rts

; FNACK -- 0 on success, FNE* on transport failure, FNENAK if the server said
; no. Call after FNGO returned 0.
FNACK:  lda     FNRCMD
        cmp     #$06
        beq     FNACK1
        lda     #FNENAK
        rts
FNACK1: lda     #FNEOK
        rts

; -----------------------------------------------------------------------
; Text, composed by the cartridge.
;
; FNROWA -- begin text row A.
FNROWA: sta     FNRSEL+FH_TROW
        rts

; FNENDR -- render the composed row.
FNENDR: lda     #0
        sta     FNRSEL+FH_TEND
        rts

; FNRRPL -- render up to Y bytes of the reply window at offset X into text
; row A, stopping at NUL.
; Cart to cart, never through the console's 128 bytes of RAM; the reply window
; is stable until the next SEQ commit or slice select. X reaches the first 256.
FNRRPL: sta     FNRSEL+FH_TROW
FNRR1:  lda     FNRPLY,x
        beq     FNRR2
        sta     FNRSEL+FH_TCHR
        inx
        dey
        bne     FNRR1
FNRR2:  lda     #0
        sta     FNRSEL+FH_TEND
        rts

; FNRSTR -- render the NUL-terminated string at FNPTRL/FNPTRH into text row A.
FNRSTR: sta     FNRSEL+FH_TROW
        ldy     #0
FNRS1:  lda     (FNPTRL),y
        beq     FNRS2
        sta     FNRSEL+FH_TCHR
        iny
        cpy     #FNTCOL
        bne     FNRS1
FNRS2:  lda     #0
        sta     FNRSEL+FH_TEND
        rts

; FNHEX -- render A as two hex digits into the row being composed.
FNHEX:  pha
        lsr     a
        lsr     a
        lsr     a
        lsr     a
        jsr     FNHEX1
        pla
        and     #$0F
FNHEX1: cmp     #10
        bcc     FNHEX2
        adc     #6              ; carry is set here, so this adds 7 in total
FNHEX2: adc     #'0'
        sta     FNRSEL+FH_TCHR
        rts

; FNPATH -- append the NUL-terminated string at FNPTRL/H to the TX stream,
; padded with NULs to EXACTLY 256 bytes.
; OPEN_DIRECTORY and SET_DEVICE_FULLPATH read exactly 256 bytes; a short
; payload fails the server's read.
FNPATH: ldy     #0
FNPA1:  lda     (FNPTRL),y
        beq     FNPA2
        sta     FNTX
        iny
        bne     FNPA1
        rts                     ; exactly 256 path bytes; no padding needed
FNPA2:  lda     #0
FNPA3:  sta     FNTX            ; Y wraps to 0 after exactly 256 - Y stores
        iny
        bne     FNPA3
        rts

; FNBLK -- arm the ROM swap (FN_REG_BOOTLOCK).
FNBLK:  lda     #FNBLKM
        ldx     #FR_BLCK
        jmp     FNRW

; FNSWAP -- boot the staged image. Does not return.
; The swap replaces the whole window, so the trigger runs from a stub at $80 in
; the only RAM, which is also the stack: keep SP above it (no JSR inside, no
; interrupts). The stub silences AUDV0/AUDV1 and returns SWACNT/SWBCNT to
; inputs, which few games set themselves.
FNSWAP: ldx     #FNSTE-FNSTB-1
FNSW1:  lda     FNSTB,x
        sta     $80,x
        dex
        bpl     FNSW1
        jmp     $0080

FNSTB:  lda     #0
        sta     GRP0            ; silence the sprites
        sta     GRP1
        sta     AUDV0           ; *** and the sound, or it screams ***
        sta     AUDV1
        sta     PF0
        sta     PF1
        sta     PF2
        sta     WSYNC           ; land on a line boundary
        sta     SWACNT          ; *** RIOT port A back to inputs ***
        sta     SWBCNT          ; *** and port B ***
        lda     #2
        sta     VBLANK          ; blank across the swap
        sta     VSYNC           ; a clean discontinuity, not a rolling frame
        sta     FNRSEL+FH_SWAP  ; THE SWAP. The next fetch is at $00xx, in
                                ;   RAM with A12 low, so the window may flip
                                ;   between this store and it.
        ldx     #$FF
        txs                     ; a clean stack for the new image
        jmp     ($1FFC)         ; the NEW image's own reset vector, exactly
                                ;   what the 6507 fetches at power-on
FNSTE:

; ---------------------------------------------------------------------------
; Building a 256-byte path payload a piece at a time.
; For a path built from a prefix plus a name in the reply window; FNPEND pads
; it to 256 bytes.
FNPBEG: lda     #0
        sta     FNPCNT
        rts

; FNPCH -- append A. Preserves X and Y.
FNPCH:  sta     FNTX
        inc     FNPCNT
        rts

; FNPSTR -- append the NUL-terminated string at FNPTRL/H.
FNPSTR: ldy     #0
FNPS1:  lda     (FNPTRL),y
        beq     FNPS2
        jsr     FNPCH
        iny
        bne     FNPS1
FNPS2:  rts

; FNPRPL -- append up to Y bytes of the reply window at offset X, stopping at
; NUL.
; Cart to cart through the TX page; the name never lands in console RAM.
FNPRPL: lda     FNRPLY,x
        beq     FNPR2
        jsr     FNPCH
        inx
        dey
        bne     FNPRPL
FNPR2:  rts

; FNPEND -- pad out to exactly 256 bytes.
FNPEND: lda     #0
FNPE1:  ldx     FNPCNT
        beq     FNPE2           ; wrapped: 256 bytes written
        sta     FNTX
        inc     FNPCNT
        bne     FNPE1
FNPE2:  rts

; ---------------------------------------------------------------------------
; The working directory, held in the CARTRIDGE.
; A 256-byte path has no room in 128 bytes of RAM, so the cart holds it, with
; its trailing "/". It survives a console RESET: call FNWRST at startup.

; FNWRST -- empty it.
FNWRST: lda     #FP_RST
        sta     FNRSEL+FH_PATHO
        rts

; FNWCH -- append A. Preserves X and Y.
FNWCH:  sta     FNRSEL+FH_PATHC
        rts

; FNWPOP -- drop the last component. "/a/b/c/" becomes "/a/b/", and "/"
; stays "/", so walking up from the root is harmless.
FNWPOP: lda     #FP_POP
        sta     FNRSEL+FH_PATHO
        rts

; FNWTX -- emit the path into the TX stream as EXACTLY 256 bytes, padded.
; One store; the cartridge does the other 255.
FNWTX:  lda     #FP_TX
        sta     FNRSEL+FH_PATHO
        rts

; FNPWD -- append the working directory to the payload being built, WITHOUT
; padding, and advance the payload counter past it.
; For SET_DEVICE_FULLPATH's one payload of directory plus name; the cart
; publishes the length it emitted at FNPLNL.
FNPWD:  lda     #FP_TXRAW
        sta     FNRSEL+FH_PATHO
        lda     FNPCNT
        clc
        adc     FNPLNL
        sta     FNPCNT
        rts

; FNWSTR -- append the NUL-terminated string at FNPTRL/H.
FNWSTR: ldy     #0
FNWS1:  lda     (FNPTRL),y
        beq     FNWS2
        sta     FNRSEL+FH_PATHC
        iny
        bne     FNWS1
FNWS2:  rts

; FNWRPL -- append up to Y bytes of the reply window at offset X, stopping at
; NUL. Cartridge to cartridge: the name never lands in console RAM.
FNWRPL: lda     FNRPLY,x
        beq     FNWR2
        sta     FNRSEL+FH_PATHC
        inx
        dey
        bne     FNWRPL
FNWR2:  rts

; FNEOF -- Z set if the reply is the $7F,$7F end-of-directory marker.
; Required: past the end an SD host repeats "..", and every later
; SET_DIRECTORY_POSITION NAKs for the session.
FNEOF:  lda     FNRPLY
        cmp     #$7F
        bne     FNEO1
        lda     FNRPLY+1
        cmp     #$7F
FNEO1:  rts

; ---------------------------------------------------------------------------
; INSCAN -- fold the joystick and console switches into one active-high byte
; and return only what is NEWLY pressed, so a held stick steps once.
INSCAN: lda     SWCHA
        eor     #$FF            ; the port is active low
        and     #$F0            ; right, left, down, up in bits 7-4
        lsr     a
        lsr     a
        lsr     a
        lsr     a
        sta     INCUR
        lda     INPT4
        bmi     INSC1           ; bit 7 high = fire not pressed
        lda     INCUR
        ora     #IN_FIRE
        sta     INCUR
INSC1:  lda     SWCHB
        and     #$01
        bne     INSC2
        lda     INCUR
        ora     #IN_RST
        sta     INCUR
INSC2:  lda     SWCHB
        and     #$02
        bne     INSC3
        lda     INCUR
        ora     #IN_SEL
        sta     INCUR
INSC3:  lda     INPREV          ; newly pressed = current AND NOT previous
        eor     #$FF
        and     INCUR
        pha
        lda     INCUR
        sta     INPREV
        pla
        rts
