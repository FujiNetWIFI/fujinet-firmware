; fujitest.asm -- M1: one real transaction, and proof the cart survives a reset.
;
; Asks the FujiNet for GET_ADAPTERCONFIG_EXTENDED and puts the live SSID, IP
; address and firmware version on screen. Nothing is faked: the bytes come off
; a socket to fujinet-pc, through the cartridge's own fujimail.c, and are
; rendered by the cartridge's own compositor.
;
; The reply is 240 bytes and the window is 512, so it lands in slice 0 whole
; and no slice paging is needed -- which is why the reply window is 512 and not
; 256 (see fuji_mailbox.h).
;
; THE TRANSACTION RUNS BEFORE THE DISPLAY STARTS, with the screen blanked.
; Under emulation the socket round trip is synchronous so this is invisible;
; on real hardware it is a brief blank frame before the picture appears. A
; client that wants to stay in sync while it talks has to do its transactions
; from inside the frame loop, which fujicfg will.
;
; Exit test: emu/drive.lua reads the rendered planes back and requires the SSID
; to be on screen; emu/resettest.lua then toggles the RESET switch and requires
; the NEXT sequence number to be ACKSEQ+1, not 1 -- the cart has no reset line,
; so it keeps its state across a console reset and a client that restarted its
; own counter would collide with a sequence already answered.

        CPU     6502
        INCLUDE "vcs.inc"

; ---------------- zero page ----------------
PAD3    EQU     $81             ; the display kernel's 3-cycle pad target
SAVSP   EQU     $82             ; stack pointer, parked across the kernel
APERR   EQU     $8C             ; what went wrong, if anything

        INCLUDE "fujinet.inc"

; Field offsets inside GET_ADAPTERCONFIG_EXTENDED, from the packed
; AdapterConfigExtended in lib/device/fujiDevice/fujiDevice.h. Every field this
; client wants is below 256, so a single-byte index reaches all of them.
AC_SSID EQU     0               ; char[33]
AC_VER  EQU     125             ; char[15]
AC_SIP  EQU     140             ; char[16], the IP already formatted as text

        ORG     $1000

; ---------------- cold start ----------------
START:  sei
        cld
        ldx     #$FF
        txs
        lda     #0
CLRLP:  sta     $00,x           ; $00-$7F is the TIA, $80-$FF is RAM
        dex
        bne     CLRLP
        sta     $00

        lda     #2              ; keep the screen blanked while we talk
        sta     VBLANK
        lda     #0
        sta     APERR

; ---------------- the transaction ----------------
        jsr     FNARM           ; open the decode gate
        jsr     FNCHK
        beq     GOTCART
        lda     #FNENOC         ; no cartridge is answering
        sta     APERR
        jmp     SHOW

GOTCART:
        lda     #FNDEVF
        sta     FNDEV
        lda     #FNCADPX
        sta     FNCMD
        lda     #0
        sta     FNNPR
        jsr     FNBEG
        jsr     FNGO
        sta     APERR
        cmp     #FNEOK
        bne     SHOW
        jsr     FNACK
        sta     APERR

; ---------------- put it on screen ----------------
SHOW:   lda     #(TTITLE)&$FF
        sta     FNPTRL
        lda     #(TTITLE)>>8
        sta     FNPTRH
        lda     #0
        jsr     FNRSTR

        lda     APERR
        beq     SHOWOK

; The failure screen is as useful as the success one: it is what a burned
; EPROM shows when there is no cartridge behind it.
        lda     #(TFAIL)&$FF
        sta     FNPTRL
        lda     #(TFAIL)>>8
        sta     FNPTRH
        lda     #2
        jsr     FNRSTR
        lda     #3
        jsr     FNROWA
        lda     APERR
        jsr     FNHEX
        jsr     FNENDR
        jmp     RUN

SHOWOK: lda     #(TSSID)&$FF
        sta     FNPTRL
        lda     #(TSSID)>>8
        sta     FNPTRH
        lda     #2
        jsr     FNRSTR
        lda     #3              ; row
        ldx     #AC_SSID        ; reply offset
        ldy     #FNTCOL         ; at most a rowful
        jsr     FNRRPL

        lda     #(TIP)&$FF
        sta     FNPTRL
        lda     #(TIP)>>8
        sta     FNPTRH
        lda     #5
        jsr     FNRSTR
        lda     #6
        ldx     #AC_SIP
        ldy     #FNTCOL
        jsr     FNRRPL

        lda     #(TVER)&$FF
        sta     FNPTRL
        lda     #(TVER)>>8
        sta     FNPTRH
        lda     #8
        jsr     FNRSTR
        lda     #9
        ldx     #AC_VER
        ldy     #FNTCOL
        jsr     FNRRPL

; The sequence number, in hex. This is the rung's real evidence: after a
; console RESET it must read 02, not 01.
        lda     #(TSEQ)&$FF
        sta     FNPTRL
        lda     #(TSEQ)>>8
        sta     FNPTRH
        lda     #11
        jsr     FNRSTR
        lda     #12
        jsr     FNROWA
        lda     FNACKS
        jsr     FNHEX
        jsr     FNENDR

RUN:    lda     #0
        sta     VBLANK
        jsr     DINIT
        jmp     DLOOP

; ---------------- strings ----------------
TTITLE: DB      "FUJINET 2600",0
TSSID:  DB      "SSID",0
TIP:    DB      "IP",0
TVER:   DB      "VERSION",0
TSEQ:   DB      "ACKSEQ",0
TFAIL:  DB      "FAILED ERR",0

        INCLUDE "fujilib.inc"
APPVBL: rts                     ; this client needs no per-frame work

        INCLUDE "fujidisp.inc"

; ---------------- the fixed tail ----------------
; $1FFC must live in the fixed half so a console RESET is survivable whatever
; bank happens to be mapped low.
        ORG     $1FFC
        DW      START           ; RESET
        DW      START           ; BRK -- a runaway reboots rather than hangs

        END
