; fujilib.inc -- the 6502 side of the FujiNet cartridge mailbox: the code.
;
; Equates are in fujinet.inc, which must be included BEFORE the ORG. This file
; is code and must be included inside it.
;
; A register write is TWO stores on this console, not one:
;
;       lda #value
;       sta FNRSEL+reg          ; arm; this store's data is ignored
;       sta FNCMT               ; commit: register `reg` = value
;
; That is not ceremony. The cartridge port has no R/W line, so the cart cannot
; tell a read of a write port from a write to it -- a stray LDA into page $1C
; is decoded as an access carrying whatever the floating bus held. Arming and
; committing separately means ONE stray mutates nothing: an arm carries no
; value, and a commit with nothing armed is discarded. The Channel F keeps the
; same pair as protocol shape; here it is the defence.
;
; The other rule that will bite: NEVER use a read-modify-write instruction on
; the control or TX pages. INC/DEC/ASL/LSR/ROL/ROR absolute are three bus
; cycles at one address -- read, write old, write new -- and the cart's "last
; value before the address changed" sampling has no defined answer in that
; burst. Only STA/STX/STY. tools/checkrom.py fails the build on the rest.

; -----------------------------------------------------------------------
; FNRW -- register X = A. The whole transport is built on this.
;
; `sta FNRSEL,x` is safe for any X: the base low byte is $00 so the index can
; never carry, which means the dummy read STA abs,X always performs lands on
; the SAME address as the write -- one parked access, not two, and an arm
; ignores its data anyway.
FNRW:   sta     FNRSEL,x
        sta     FNCMT
        rts

; FNARM -- open the decode gate.
; Two stores, in order, carrying two specific values. A 7800's BIOS probes
; cartridge space on startup and cannot produce that, which is the point.
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
; The stream is NPARAM x { size byte, then that many value bytes } then the
; payload, identical to every sibling port.
FNPB:   pha
        lda     #1
        sta     FNTX
        pla
        sta     FNTX
        rts

; FNPW -- append a two-byte little-endian parameter. A = low, X = high.
;
; SET_DIRECTORY_POSITION takes ONE parameter of TWO bytes -- fujiDevice reads
; it as packet.param(0) declared uint16_t -- not two one-byte parameters, and
; getting that wrong produces a NAK with nothing to point at.
FNPW:   pha
        lda     #2
        sta     FNTX
        pla
        sta     FNTX
        txa
        sta     FNTX
        rts

; FNGO -- commit and wait. Returns A = FNE* (0 = the transaction completed).
;
; THE RULE THAT EVERY PORT IN THIS FAMILY LEARNED THE HARD WAY: the next
; sequence number comes from the CART'S OWN persisted ACKSEQ + 1, never from a
; counter in RAM. A console RESET restarts the client and re-zeroes its
; variables but does NOT reset the cartridge -- there is no reset line on the
; connector and the RESET switch is a bit in a RIOT register the cart never
; sees. A program-local counter would restart at 1, collide with a sequence
; already answered, and the transaction would be dropped in silence.
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
        ; ACKSEQ is published LAST, after the whole reply is in place, so a
        ; match means everything behind it is already readable.
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
;
; This is the zero-copy path and on this console it is not an optimisation.
; The reply window is repainted only by a SEQ commit or a slice select, so it
; is stable while the row is streamed -- and the bytes go from the cartridge
; back to the cartridge without ever occupying any of the console's 128 bytes
; of RAM, of which the stack already owns the top.
;
; X is a single byte, so this reaches the first 256 bytes of the reply. Every
; field of GET_ADAPTERCONFIG_EXTENDED that a client wants lives below that.
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
;
; Not optional and not a rounding: OPEN_DIRECTORY and SET_DEVICE_FULLPATH both
; read exactly 256 bytes and a short payload fails the server's read. Every
; port in this family has paid for this one.
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
;
; The swap replaces every byte of the 4K window including the code that
; triggered it, so the trigger has to run from somewhere else -- and on this
; console the only RAM is $80-$FF, which is ALSO the stack ($0100-$01FF
; mirrors into it). The stub lives at $80 and the caller must keep SP above
; it; there is no JSR inside the stub and the 6507 has no interrupts, so
; nothing else can touch the stack while it runs.
;
; Three of the stub's stores are easy to leave out and each breaks a different
; thing: AUDV0/AUDV1, or a tone screams until the booted game's cold start
; runs; and SWACNT/SWBCNT back to inputs, because almost no game writes them
; and a client that left the RIOT ports as outputs would brick every game
; booted after it.
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
;
; SET_DEVICE_FULLPATH and OPEN_DIRECTORY both read EXACTLY 256 bytes, so the
; payload has to be padded whatever it is built from. FNPATH does that for a
; string already in ROM; these do it for a path assembled from a prefix plus a
; filename that only exists in the cartridge's reply window.
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
;
; This is the zero-copy path, and on this console it is not an optimisation.
; The name being appended is sitting in the cartridge's reply window; it goes
; straight back to the cartridge through the TX page without ever occupying
; any of the console's 128 bytes of RAM, of which the stack already owns the
; top and the swap stub the bottom.
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
;
; A browser has to remember which directory it is in. This console has 128
; bytes of RAM, the stack owns the top of them and the swap stub the bottom,
; and a path is 256 bytes. So the path lives in the cartridge: built a
; character at a time, popped a component at a time, and emitted straight into
; the TX stream when a transaction needs it. It never occupies console RAM.
;
; A directory is held WITH its trailing "/", so appending a filename to it is
; just FNWRPL and needs no separator logic here.
;
; IT SURVIVES A CONSOLE RESET, because there is no reset line on this
; connector and the cartridge never sees the switch. That is the behaviour you
; want from a browser -- but it means a client must not assume the buffer is
; empty at startup. Call FNWRST.

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
;
; This is the half of the path buffer that SET_DEVICE_FULLPATH needs: the
; directory and the filename go into ONE 256-byte payload, so the cartridge
; emits its buffer raw and FNPEND pads what is left. The console cannot know
; how many bytes that was, so the cartridge publishes it at FNPLNL.
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
;
; Checking this is NOT optional. Past the end, fujiDevice returns $7F,$7F --
; but a fujinet-pc SD host returns ".." over and over, and once a client has
; read past the end every later SET_DIRECTORY_POSITION NAKs for the rest of
; the session.
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
