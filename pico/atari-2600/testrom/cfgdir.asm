; cfgdir.asm -- CONFIG bank 1: the directory browser.
;
; Up and down move the cursor. Left and right page. FIRE descends into a
; folder or boots a file. SELECT goes back up a level, and up from the root
; goes back to the host screen.
;
; THE WORKING DIRECTORY IS NOT HERE. It is 256 bytes in the cartridge, and
; this console has 128 bytes of RAM with the stack in the top of them, so it
; could not be here. Descending is: append the entry's name to the cartridge's
; buffer, straight from the cartridge's reply window, and re-list. Going up is
; one store -- the cartridge knows where the last separator was, so this does
; not have to.
;
; No filename ever lands in console RAM either. A name goes cartridge -> text
; plane to be drawn, and cartridge -> path buffer to be entered, and this
; program only ever holds an index. That is the ColecoVision port's lesson
; taken as far as it goes.
;
; The cost is that moving the cursor re-lists the directory: with no names in
; RAM there is nothing to redraw from. Under emulation that is instant; on
; real hardware it is about a second a step, and the fix when it matters is a
; cartridge-side page cache, which is the same shape of problem the blit port
; already solves.

        CPU     6502
        INCLUDE "vcs.inc"

PAD3    EQU     $81
SAVSP   EQU     $82

        INCLUDE "fujinet.inc"
        INCLUDE "cfgdefs.inc"

        ORG     $1000

START:  lda     #2
        sta     VBLANK
        lda     #0
        sta     CFERR
        sta     CFSTEP

        jsr     DRAW
        lda     #0
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
        cmp     CFCNT
        bcs     AVDONE
        sta     CFSEL
        jmp     AVRE

AV1:    lda     CFKEY
        and     #IN_UP
        beq     AV2
        lda     CFSEL
        beq     AVDONE
        sec
        sbc     #1
        sta     CFSEL
        jmp     AVRE

AV2:    lda     CFKEY
        and     #IN_RIGHT
        beq     AV3
        lda     CFFULL          ; only if the page we are on was full: paging
        beq     AVDONE          ;   past the end NAKs for the rest of the
        inc     CFPAGE          ;   session on an SD host
        lda     #0
        sta     CFSEL
        jmp     AVRE

AV3:    lda     CFKEY
        and     #IN_LEFT
        beq     AV4
        lda     CFPAGE
        beq     AVDONE
        dec     CFPAGE
        lda     #0
        sta     CFSEL
        jmp     AVRE

AV4:    lda     CFKEY
        and     #IN_SEL
        beq     AV5
        jmp     GOUP

AV5:    lda     CFKEY
        and     #IN_FIRE
        beq     AVDONE
        jmp     PICK
AVDONE: rts

AVRE:   lda     #2              ; blank while we talk; a listing is many round
        sta     VBLANK          ;   trips and the frame will not finish
        jsr     DRAW
        lda     #0
        sta     VBLANK
        rts

; ---------------------------------------------------------------------------
; GOUP -- back up a level, or back to the host screen from the root.
GOUP:   lda     CFDEPTH
        bne     GOUP1
        lda     #BANKHST
        jmp     CFGOTO          ; does not return
GOUP1:  dec     CFDEPTH
        jsr     FNWPOP
        lda     #0
        sta     CFPAGE
        sta     CFSEL
        jmp     AVRE

; ---------------------------------------------------------------------------
; ODIR -- OPEN_DIRECTORY(host, <the cartridge's path>). Z set on success.
;
; One parameter, then a 256-byte payload emitted by the cartridge from its own
; buffer. FNWTX is a single store and the other 255 bytes never cross the bus.
ODIR:   lda     #FNDEVF
        sta     FNDEV
        lda     #FNCODIR
        sta     FNCMD
        lda     #1
        sta     FNNPR
        jsr     FNBEG
        lda     CFHOST
        jsr     FNPB
        jsr     FNWTX
        jsr     FNGO
        sta     CFERR
        cmp     #FNEOK
        bne     ODE
        jsr     FNACK
        sta     CFERR
        cmp     #FNEOK
        beq     ODOK
ODE:    lda     #2
        sta     CFSTEP
        lda     #1
        rts
ODOK:   rts                     ; A is FNEOK, so Z is set

; ---------------------------------------------------------------------------
; SEEK -- SET_DIRECTORY_POSITION(CFPAGE * NROWS). Z set on success, and a no-op
; on page 0 so the common case costs no round trip.
;
; The position is ONE parameter of TWO bytes -- fujiDevice reads it as
; packet.param(0) declared uint16_t -- so it is {size 2, lo, hi}, not two
; one-byte parameters.
SEEK:   lda     CFPAGE
        bne     SEEK1
        lda     #FNEOK
        rts
SEEK1:  lda     #FNDEVF
        sta     FNDEV
        lda     #FNCSDPS
        sta     FNCMD
        lda     #1
        sta     FNNPR
        jsr     FNBEG
        jsr     POSLO           ; A = low byte, X = high byte
        jsr     FNPW
        jsr     FNGO
        sta     CFERR
        cmp     #FNEOK
        bne     SKE
        jsr     FNACK
        sta     CFERR
        cmp     #FNEOK
        beq     SKOK
SKE:    lda     #3
        sta     CFSTEP
        lda     #1
        rts
SKOK:   rts

; POSLO -- A = low byte and X = high byte of CFPAGE * NROWS. NROWS is 14, so
; 14 pages fit in a byte and the high byte only matters past entry 255.
POSLO:  lda     #0
        sta     CFWANT          ; scratch: the high byte accumulates here
        lda     CFPAGE
        beq     POSL2
        tax
        lda     #0
POSL1:  clc
        adc     #NROWS
        bcc     POSL3
        inc     CFWANT
POSL3:  dex
        bne     POSL1
POSL2:  ldx     CFWANT
        rts

; ---------------------------------------------------------------------------
; RDENT -- READ_DIR_ENTRY(NAMELN, 0). Z set on success; the entry stays in the
; reply window and is not copied anywhere.
RDENT:  lda     #FNDEVF
        sta     FNDEV
        lda     #FNCRDIR
        sta     FNCMD
        lda     #2
        sta     FNNPR
        jsr     FNBEG
        lda     #NAMELN
        jsr     FNPB
        lda     #0
        jsr     FNPB
        jsr     FNGO
        cmp     #FNEOK
        bne     RDE
        jsr     FNACK
        cmp     #FNEOK
        bne     RDE
        lda     #FNEOK
        rts
RDE:    sta     CFERR
        lda     #4
        sta     CFSTEP
        lda     #1
        rts

; ---------------------------------------------------------------------------
; DRAW -- re-list the current directory page.
DRAW:   lda     #(TTITLE)&$FF
        sta     FNPTRL
        lda     #(TTITLE)>>8
        sta     FNPTRH
        lda     #0
        jsr     FNRSTR
        lda     #1
        jsr     FNROWA
        lda     #'P'
        sta     FNRSEL+FH_TCHR
        lda     CFPAGE
        jsr     FNHEX
        jsr     FNENDR

        jsr     ODIR
        bne     DRBAD
        jsr     SEEK
        bne     DRBAD

        lda     #0
        sta     CFCNT
        sta     CFFULL
        ldx     #0
DRLOOP: stx     CFWANT
        jsr     RDENT
        bne     DRBAD
        jsr     FNEOF
        beq     DREND           ; the $7F,$7F marker: stop, and do NOT read on
        ldx     CFWANT

        txa
        clc
        adc     #ROW0
        jsr     FNROWA
        lda     CFWANT
        cmp     CFSEL
        bne     DRNC
        lda     #'>'
        jmp     DRC
DRNC:   lda     #' '
DRC:    sta     FNRSEL+FH_TCHR
        ldx     #0
        ldy     #FNTCOL-1
        jsr     PUTRPL
        jsr     FNENDR

        ldx     CFWANT
        inx
        stx     CFCNT
        cpx     #NROWS
        bcc     DRLOOP
        lda     #1              ; the page filled, so there may be another
        sta     CFFULL

DREND:  ldx     CFCNT
DRBL:   cpx     #NROWS
        bcs     DRHINT
        txa
        clc
        adc     #ROW0
        jsr     FNROWA
        jsr     FNENDR
        inx
        jmp     DRBL
DRHINT: lda     #(THINT)&$FF
        sta     FNPTRL
        lda     #(THINT)>>8
        sta     FNPTRH
        lda     #ROW0+NROWS+1
        jsr     FNRSTR
        ; The cursor may be past the end of a short page.
        lda     CFCNT
        beq     DROK
        cmp     CFSEL
        bcs     DROK
        sec
        sbc     #1
        sta     CFSEL
DROK:   lda     #0
        rts
DRBAD:  jsr     SHOWERR
        lda     #1
        rts

; PUTRPL -- append up to Y bytes of the reply window at offset X.
PUTRPL: lda     FNRPLY,x
        beq     PUTR2
        sta     FNRSEL+FH_TCHR
        inx
        dey
        bne     PUTRPL
PUTR2:  rts

; ---------------------------------------------------------------------------
; PICK -- descend into the selection, or boot it.
;
; The name is not in RAM, so the directory is re-opened and re-read up to the
; selection. What comes back is examined IN THE REPLY WINDOW: a trailing "/"
; means a folder.
PICK:   lda     #2
        sta     VBLANK
        jsr     ODIR
        beq     PK1
        jmp     PKBAD
PK1:    jsr     SEEK
        beq     PK2
        jmp     PKBAD
PK2:    lda     #0
        sta     CFWANT
PKSKIP: jsr     RDENT
        beq     PKS1
        jmp     PKBAD
PKS1:   jsr     FNEOF
        bne     PKS2
        jmp     PKBAD           ; ran off the end: the selection is gone
PKS2:   lda     CFWANT
        cmp     CFSEL
        beq     PKGOT
        inc     CFWANT
        jmp     PKSKIP

; Folder or file? Walk to the NUL and look at the byte before it.
PKGOT:  ldx     #0
PKL1:   lda     FNRPLY,x
        beq     PKL2
        inx
        cpx     #NAMELN
        bcc     PKL1
PKL2:   cpx     #0
        bne     PKL3
        jmp     PKBAD           ; an empty name is nothing to act on
PKL3:   dex
        lda     FNRPLY,x
        cmp     #'/'
        beq     PKDIR
        jmp     PKFILE

; ---- descend ---------------------------------------------------------------
; The name goes from the cartridge's reply window straight into the
; cartridge's path buffer. It is already held with its trailing "/", which is
; exactly the form the buffer wants, so appending it is the whole operation.
PKDIR:  ldx     #0
        ldy     #NAMELN
        jsr     FNWRPL
        inc     CFDEPTH
        lda     #0
        sta     CFPAGE
        sta     CFSEL
        jsr     DRAW
        lda     #0
        sta     VBLANK
        rts

; ---- boot ------------------------------------------------------------------
; SET_DEVICE_FULLPATH(dev, host, mode, <path> + <name>). The path payload is
; built from the cartridge's own buffer plus the name from its own reply
; window; neither ever crosses into console RAM.
PKFILE: lda     #FNDEVF
        sta     FNDEV
        lda     #FNCSDFP
        sta     FNCMD
        lda     #3
        sta     FNNPR
        jsr     FNBEG
        lda     #DEVSLOT
        jsr     FNPB
        lda     CFHOST
        jsr     FNPB
        lda     #FMREAD
        jsr     FNPB
        jsr     FNPBEG
        jsr     FNPWD           ; the working directory, verbatim
        ldx     #0
        ldy     #NAMELN
        jsr     FNPRPL          ; then the filename
        jsr     FNPEND
        jsr     FNGO
        sta     CFERR
        cmp     #FNEOK
        bne     PKBAD2
        jsr     FNACK
        sta     CFERR
        cmp     #FNEOK
        bne     PKBAD2

        lda     #FNDEVF
        sta     FNDEV
        lda     #FNCMIMG
        sta     FNCMD
        lda     #2
        sta     FNNPR
        jsr     FNBEG
        lda     #DEVSLOT
        jsr     FNPB
        lda     #FMREAD
        jsr     FNPB
        jsr     FNGO
        sta     CFERR
        cmp     #FNEOK
        bne     PKBAD2
        jsr     FNACK
        sta     CFERR
        cmp     #FNEOK
        bne     PKBAD2

PKWAIT: ldy     #0
PKW1:   ldx     #0
PKW2:   lda     FNBST
        cmp     #FNBRDY
        beq     PKBOOT
        cmp     #FNBFAIL
        beq     PKBAD2
        dex
        bne     PKW2
        dey
        bne     PKW1
        lda     #FNEWAIT
        sta     CFERR
PKBAD2: lda     #5
        sta     CFSTEP
PKBAD:  jsr     SHOWERR
        lda     #0
        sta     VBLANK
        rts

PKBOOT: jsr     FNBLK
        jmp     FNSWAP          ; does not return

; ---------------------------------------------------------------------------
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

TTITLE: DB      "FN BROWSE",0
THINT:  DB      "SEL=UP",0

        INCLUDE "fujilib.inc"
        INCLUDE "fujidisp.inc"

        END
