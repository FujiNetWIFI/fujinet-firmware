; fujidisp.inc -- the 48-pixel text display, shared by every 2600 client.
;
; The cartridge composes the glyphs (firmware/src/vcs_render.c) and publishes
; them as six 128-byte planes at $1800-$1AFF; this is the console half, which
; does nothing but stream them into GRP0/GRP1 on a cycle-exact schedule.
;
; DINIT sets the TIA up and positions the players. DLOOP runs frames forever.
; Everything about WHY the constants are what they are is in the comments
; below -- they are the coordinates of a one-pixel-wide window, found by
; byte-comparing the raster against the renderer with emu/dispcheck.py.

; ---------------- kernel timing constants ----------------
; These four are not taste. The 48-pixel block is six player copies 2.67 cycles
; apart and the kernel's stores are 3 cycles, so where the block sits decides
; whether each graphics update lands between copies or inside one. The window
; that satisfies all six groups is ONE PIXEL wide, and these are its
; coordinates, found by byte-comparing the raster against the renderer
; (emu/dispcheck.py) rather than by eye -- every wrong setting still looks like
; text.
RNOP    EQU     16              ; NOPs before RESP0 (2 cycles each)...
R3      EQU     1               ; ...plus this, so RESP0 lands on cycle 38
HM0     EQU     15              ; HMP0 = $F0: move P0 right one pixel...
HM1     EQU     0               ; ...and leave P1, closing the gap to eight
VDEL    EQU     1               ; vertical delay on both players

; ---------------- text plane bases ----------------
; FN_T_PLANE(0..5) from firmware/include/fuji_mailbox.h.
TP0     EQU     $1800
TP1     EQU     $1880
TP2     EQU     $1900
TP3     EQU     $1980
TP4     EQU     $1A00
TP5     EQU     $1A80

TROWS   EQU     21              ; FN_T_ROWS
TCELLH  EQU     6               ; FN_T_CELL_H
TLINES  EQU     TROWS*TCELLH    ; 126 scanlines of text

; NTSC: 3 vsync + 37 vblank + 192 visible + 30 overscan = 262.
VBLINES EQU     37
OSLINES EQU     30
PADLINE EQU     (192-TLINES)/2  ; 33 above and below


; ---------------- DINIT: TIA setup and player positioning ----------------
DINIT:
; Two players, three copies each, eight pixels apart: P0 draws groups 0,2,4
; and P1 groups 1,3,5, interleaving to 48 contiguous pixels. VDELP is what
; makes the six-write sequence land in the right copies.
        lda     #$03            ; NUSIZ: three copies, close
        sta     NUSIZ0
        sta     NUSIZ1
        lda     #VDEL
        sta     VDELP0
        sta     VDELP1
        lda     #$0E            ; white
        sta     COLUP0
        sta     COLUP1
        lda     #$00
        sta     COLUBK

; Player positioning, transcribed from batari Basic's std_kernel.asm rather
; than re-derived. That kernel is proven, and its setup is exact: RESP0 strobed
; at cycle 43, RESP1 three cycles later at 46 (nine pixels, since two stores
; cannot be closer), HMP0 = $E0 and HMP1 = $F0 to pull them to eight apart, and
; HMOVE at cycle 73 -- late in the same line, NOT immediately after a WSYNC.
;
; RDLY is the delay before the RESP0 store, in cycles, built from NOPs plus an
; optional 3-cycle zero-page store so that every value is reachable: the copies
; are 2.67 cycles apart, so this has to tune to the cycle, and a NOP alone only
; gets even numbers. RESP0 therefore completes at RDLY+3; batari's 43 is
; RDLY = 40.
        sta     WSYNC
        REPT    RNOP
        nop
        ENDM
        IF      R3
        sta     PAD3            ; 3 cycles, so odd delays are reachable
        ENDIF
        sta     RESP0
        sta     RESP1
        lda     #HM0*16
        sta     HMP0
        lda     #(HM1*16)&$FF
        sta     HMP1
; An EARLY HMOVE -- WSYNC, then strobe -- rather than batari's late one at
; cycle 73. A late HMOVE does not apply the motion nibbles cleanly: nudging
; HMP0 by one here widened the player gap from 8 pixels to 9 (the block grew
; from 43 to 44 pixels of span) instead of sliding the block. With the early
; strobe the nibbles behave, and one-pixel steps are what this kernel needs --
; the window between group 0 and group 5 failing is a single pixel wide.
        sta     WSYNC
        sta     HMOVE

        rts

; ---------------- DLOOP: draw frames, forever ----------------
DLOOP:
; ---------------- frame ----------------
        lda     #2
        sta     VSYNC
        sta     WSYNC
        sta     WSYNC
        sta     WSYNC
        lda     #0
        sta     VSYNC

        lda     #2
        sta     VBLANK
        ldx     #VBLINES
VBLP:   sta     WSYNC
        dex
        bne     VBLP
; The client's per-frame hook, called while the screen is still blanked and
; the raster is nowhere near the text. Input scanning belongs here; so does a
; transaction, with the caveat that a network round trip takes far longer than
; a frame and the picture will stall for it. Clients that need nothing define
; APPVBL as an RTS.
        jsr     APPVBL

        lda     #0
        sta     VBLANK

        ldx     #PADLINE
PADA:   sta     WSYNC
        dex
        bne     PADA

; ---------------- the kernel ----------------
; 53 cycles of the 76 available. Y is never clobbered: the textbook score
; kernel reloads it from scratch because there it holds the font row, but here
; Y is the screen-wide scanline index and must survive the whole frame.
; The stack pointer is a spare register here, and that is not a flourish: the
; six player copies are 2.67 cycles apart while a zero-page store is 3, so the
; four late writes drift a full cycle across the block. Recovering a byte
; from zero page costs 3 cycles; `tsx` costs 2. That one cycle is the whole difference between a
; schedule that closes and one that does not -- it is why batari Basic's kernel
; parks a byte in S with `txs` and recovers it with `tsx`, and why the seventh
; write is `sty GRP0`: its value is a don't-care, so Y (the scanline counter)
; serves and no register need be spared for it.
;
; Nothing here may use the stack between the txs and the restore below -- no
; JSR, and the 6507 has no interrupts to worry about.
        tsx
        stx     SAVSP
        ldy     #0
KERN:   sta     WSYNC
        lda     TP0,y           ; 4
        sta     GRP0            ; 3   ->  7   new GRP0 = group 0
        lda     TP1,y           ; 4   -> 11
        sta     GRP1            ; 3   -> 14   displays group 0
        lda     TP2,y           ; 4   -> 18
        sta     GRP0            ; 3   -> 21   displays group 1
        lda     TP4,y           ; 4   -> 25
        tax                     ; 2   -> 27
        txs                     ; 2   -> 29   park group 4 in S
        lda     TP3,y           ; 4   -> 33
        tax                     ; 2   -> 35
        lda     TP5,y           ; 4   -> 39
        stx     GRP1            ; 3   -> 42   displays group 2
        tsx                     ; 2   -> 44   recover group 4
        stx     GRP0            ; 3   -> 47   displays group 3
        sta     GRP1            ; 3   -> 50   displays group 4
        sty     GRP0            ; 3   -> 53   displays group 5; value unused
        iny
        cpy     #TLINES
        bne     KERN
        ldx     SAVSP
        txs

; Blank both players. Three writes, not two: with VDELP set, a write to GRP0
; only reloads the DELAYED GRP1 and vice versa, so clearing both displayed
; registers takes GRP0, GRP1, GRP0. Toggling VDELP off and back on instead --
; the obvious-looking version -- RESTORES the stale delayed registers, and the
; padding lines above the text then redraw the last kernel line.
        lda     #0
        sta     GRP0
        sta     GRP1
        sta     GRP0

        ldx     #PADLINE
PADB:   sta     WSYNC
        dex
        bne     PADB

        lda     #2
        sta     VBLANK
        ldx     #OSLINES
OSLP:   sta     WSYNC
        dex
        bne     OSLP
        jmp     DLOOP

