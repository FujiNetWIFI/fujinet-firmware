; fujidisp.inc -- the console half of the 48-pixel text display, for every client.
;
; Streams the cart-composed planes at $1800-$1AFF into GRP0/GRP1 on a cycle-exact
; schedule. DINIT sets up the TIA and players; DLOOP runs frames forever.

; ---------------- kernel timing constants ----------------
; Copies are 2.67 cycles apart and stores 3, so the block's position decides
; whether each update lands between copies. The window that works for all six
; groups is one pixel wide; these values were found with emu/dispcheck.py, since
; every wrong setting still looks like text.
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

; Positioning after batari Basic's std_kernel.asm: RESP0 at cycle 43, RESP1 at
; 46 (nine pixels; two stores cannot be closer), HMP0 = $E0 and HMP1 = $F0 to
; close them to eight. RDLY is the cycles before the RESP0 store, from NOPs plus
; an optional 3-cycle store so odd values are reachable; batari's 43 is RDLY = 40.
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
; An early HMOVE (WSYNC, then strobe), not batari's late one at cycle 73: late,
; nudging HMP0 widened the player gap instead of sliding the block by a pixel.
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
; The client's per-frame hook, run while the screen is blanked: input, or a
; transaction (the picture stalls for its round trip). Otherwise an RTS.
        jsr     APPVBL

        lda     #0
        sta     VBLANK

        ldx     #PADLINE
PADA:   sta     WSYNC
        dex
        bne     PADA

; ---------------- the kernel ----------------
; 53 of 76 cycles. Y is the screen-wide scanline index and survives the frame.
; The four late writes drift a cycle across the block, so a byte is parked in S
; (`txs`, recovered by a 2-cycle `tsx` where zero page costs 3), as batari Basic
; does. The seventh write's value is a don't-care: `sty GRP0`. No stack use (no
; JSR) between the txs and the restore below.
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

; Three writes: with VDELP set a GRP0 write loads the delayed GRP1 and vice
; versa. Toggling VDELP instead restores the stale delayed registers.
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

