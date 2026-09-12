; cfghost.asm -- CONFIG bank 0: the host slots.
;
; READ_HOST_SLOTS returns MAX_HOSTS * MAX_HOSTNAME_LEN = 8 * 32 = 256 bytes,
; which lands in slice 0 of the 512-byte reply window whole. Slot n's name is
; at reply offset n*32, so a row index IS a slot index and there is no
; row-to-slot map to keep -- which matters, because there is nowhere to keep
; one. Empty slots are drawn as such and refuse to mount.
;
; FIRE mounts the slot, points the cartridge's working directory at "/", and
; enters the browser. SELECT goes to the adapter info screen.

        CPU     6502
        INCLUDE "vcs.inc"

PAD3    EQU     $81             ; the display kernel's 3-cycle pad target
SAVSP   EQU     $82

        INCLUDE "fujinet.inc"
        INCLUDE "cfgdefs.inc"

        ORG     $1000

; Entered from CFGOTO with the gate already open and the zero page already
; cleared by CFCOLD -- but NOT on a return from another bank, so nothing here
; may assume a zeroed variable it did not set itself.
START:  lda     #2
        sta     VBLANK
        lda     #0
        sta     CFSEL
        sta     CFERR
        sta     CFSTEP
        sta     CFDEPTH

        jsr     FNCHK
        beq     HAVE
        lda     #FNENOC
        sta     CFERR
        jmp     SHOWN

HAVE:   jsr     DRAW
SHOWN:  lda     #0
        sta     VBLANK
        jsr     DINIT
        jmp     DLOOP

; ---------------------------------------------------------------------------
APPVBL: jsr     INSCAN
        sta     CFKEY
        and     #IN_DOWN
        beq     AV1
        lda     CFSEL
        clc
        adc     #1
        cmp     #NHOSTS
        bcs     AVDONE
        sta     CFSEL
        jmp     AVCUR
AV1:    lda     CFKEY
        and     #IN_UP
        beq     AV2
        lda     CFSEL
        beq     AVDONE
        sec
        sbc     #1
        sta     CFSEL
        jmp     AVCUR
AV2:    lda     CFKEY
        and     #IN_SEL
        beq     AV3
        lda     #BANKINF
        jmp     CFGOTO          ; does not return
AV3:    lda     CFKEY
        and     #IN_FIRE
        beq     AVDONE
        jmp     PICK
AVDONE: rts

; Moving the cursor redraws the two rows that changed and nothing else. The
; names are still sitting in the cartridge's reply window -- READ_HOST_SLOTS
; has not been re-issued -- so this costs no round trip and no RAM.
AVCUR:  jsr     DRAWL
        rts

; ---------------------------------------------------------------------------
; RDHOST -- READ_HOST_SLOTS. Z set on success; the reply stays in the window.
RDHOST: lda     #FNDEVF
        sta     FNDEV
        lda     #FNCRHST
        sta     FNCMD
        lda     #0
        sta     FNNPR
        jsr     FNBEG
        jsr     FNGO
        sta     CFERR
        cmp     #FNEOK
        bne     RDHE
        jsr     FNACK
        sta     CFERR
        cmp     #FNEOK
        beq     RDHOK
RDHE:   lda     #1
        sta     CFSTEP
        lda     #1
        rts
RDHOK:  lda     #0
        rts

; ---------------------------------------------------------------------------
; DRAW -- the whole screen.
DRAW:   lda     #(TTITLE)&$FF
        sta     FNPTRL
        lda     #(TTITLE)>>8
        sta     FNPTRH
        lda     #0
        jsr     FNRSTR

        jsr     RDHOST
        bne     DRBAD
        lda     #NHOSTS
        sta     CFCNT
        jsr     DRAWL
        lda     #(THINT)&$FF
        sta     FNPTRL
        lda     #(THINT)>>8
        sta     FNPTRH
        lda     #ROW0+NHOSTS+1
        jsr     FNRSTR
        lda     #0
        rts
DRBAD:  jsr     SHOWERR
        lda     #1
        rts

; DRAWL -- the eight list rows, from the reply window still in place.
DRAWL:  ldx     #0
DRL1:   stx     CFWANT
        txa
        clc
        adc     #ROW0
        jsr     FNROWA

        ldx     CFWANT
        cpx     CFSEL
        bne     DRLNC
        lda     #'>'
        jmp     DRLC
DRLNC:  lda     #' '
DRLC:   sta     FNRSEL+FH_TCHR

        ; Reply offset = slot * 32. Eight slots at 32 bytes is 256, so the
        ; high byte is never needed and a single-byte index reaches them all.
        lda     CFWANT
        asl     a
        asl     a
        asl     a
        asl     a
        asl     a               ; * HOSTSLN
        tax
        lda     FNRPLY,x
        bne     DRLNM
        ; An empty slot. Say so rather than leaving a blank the cursor can
        ; sit on with no explanation.
        lda     #(TEMPTY)&$FF
        sta     FNPTRL
        lda     #(TEMPTY)>>8
        sta     FNPTRH
        jsr     PUTSTR
        jmp     DRLEND
DRLNM:  ldy     #FNTCOL-1
        jsr     PUTRPL
DRLEND: jsr     FNENDR
        ldx     CFWANT
        inx
        cpx     #NHOSTS
        bcc     DRL1
        rts

; ---------------------------------------------------------------------------
; PICK -- mount the selected host and enter the browser.
PICK:   lda     #2
        sta     VBLANK

        ; Refuse an empty slot. The name is in the reply window from the last
        ; READ_HOST_SLOTS, which nothing has disturbed.
        lda     CFSEL
        asl     a
        asl     a
        asl     a
        asl     a
        asl     a
        tax
        lda     FNRPLY,x
        bne     PICK1
        lda     #0
        sta     VBLANK
        rts                     ; nothing mounted, nothing said: the row
                                ;   already reads (EMPTY)
PICK1:  lda     CFSEL
        sta     CFHOST

        lda     #FNDEVF
        sta     FNDEV
        lda     #FNCMHST
        sta     FNCMD
        lda     #1
        sta     FNNPR
        jsr     FNBEG
        lda     CFHOST
        jsr     FNPB
        jsr     FNGO
        sta     CFERR
        cmp     #FNEOK
        bne     PICKB
        jsr     FNACK
        sta     CFERR
        cmp     #FNEOK
        bne     PICKB

        ; The working directory: the root of the host we just mounted. It
        ; lives in the CARTRIDGE, so the browser inherits it across the bank
        ; switch without a byte of it passing through console RAM.
        jsr     FNWRST
        lda     #'/'
        jsr     FNWCH

        lda     #0
        sta     CFPAGE
        sta     CFSEL
        sta     CFDEPTH
        lda     #BANKDIR
        jmp     CFGOTO          ; does not return

PICKB:  lda     #2
        sta     CFSTEP
        jsr     SHOWERR
        lda     #0
        sta     VBLANK
        rts

; ---------------------------------------------------------------------------
; PUTSTR -- append the string at FNPTRL/H to the row being composed.
; (FNRSTR selects the row itself; these do not, so a cursor can go first.)
PUTSTR: ldy     #0
PUTS1:  lda     (FNPTRL),y
        beq     PUTS2
        sta     FNRSEL+FH_TCHR
        iny
        cpy     #FNTCOL-1
        bne     PUTS1
PUTS2:  rts

; PUTRPL -- append up to Y bytes of the reply window at offset X.
PUTRPL: lda     FNRPLY,x
        beq     PUTR2
        sta     FNRSEL+FH_TCHR
        inx
        dey
        bne     PUTRPL
PUTR2:  rts

SHOWERR:
        lda     #ROWERR
        jsr     FNROWA
        lda     #'E'
        sta     FNRSEL+FH_TCHR
        lda     CFSTEP
        jsr     FNHEX
        lda     #' '
        sta     FNRSEL+FH_TCHR
        lda     CFERR
        jsr     FNHEX
        jsr     FNENDR
        rts

TTITLE: DB      "FN HOSTS",0
TEMPTY: DB      "(EMPTY)",0
THINT:  DB      "SEL=INFO",0

        INCLUDE "fujilib.inc"
        INCLUDE "fujidisp.inc"

        END
