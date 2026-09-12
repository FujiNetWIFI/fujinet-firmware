; fujidir.asm -- M3: browse a host's directory and boot what you pick.
;
; MOUNT_HOST -> OPEN_DIRECTORY -> READ_DIR_ENTRY x N, one entry rendered
; straight from the reply window into a text row. Joystick up/down moves the
; cursor, FIRE mounts the selection and boots it.
;
; NO FILENAME IS EVER HELD IN CONSOLE RAM. There is nowhere to put one: 128
; bytes total, the stack owns the top and the swap stub the bottom. So a name
; goes cartridge -> text plane to be drawn, and cartridge -> TX page to be
; mounted, and the console only ever holds an index. That is the ColecoVision
; port's lesson, and this console needs it more.
;
; The cost of that is what moving the cursor does: with no names in RAM there
; is nothing to redraw from, so the list is re-read from the host. Under
; emulation that is instant; on real hardware it is about a second per step.
; The fix, when CONFIG needs it, is a cartridge-side page cache -- the blit
; port already exists for exactly this shape of problem.
;
; Exit test: emu/dirtest.lua walks the cursor to a NAMED file by reading the
; rendered planes back, presses FIRE, and requires the served window to be
; byte-identical to that file.

        CPU     6502
        INCLUDE "vcs.inc"

; ---------------- zero page ----------------
; $80-$A6 is the swap stub's landing ground and $A8+ is ours; the stack runs
; down from $FF and must stay above $C0.
PAD3    EQU     $81
SAVSP   EQU     $82
DIRSEL  EQU     $AA             ; cursor row, 0-based
DIRCNT  EQU     $AB             ; entries actually on screen
APERR   EQU     $AC
APSTEP  EQU     $AD
DIRWANT EQU     $AE             ; entry index wanted while re-reading

        INCLUDE "fujinet.inc"

HOSTSL  EQU     0               ; host slot 0 = SD on the standard fujinet-pc
DEVSLOT EQU     0
NAMELN  EQU     30              ; READ_DIR_ENTRY maxlen. NOT 31: at exactly 31
                                ;   the firmware prepends icon bytes.
NROWS   EQU     14              ; list rows, leaving a title and a footer
ROW0    EQU     3               ; first list row

        ORG     $1000

START:  sei
        cld
        ldx     #$FF
        txs
        lda     #0
CLRLP:  sta     $00,x
        dex
        bne     CLRLP
        sta     $00

        lda     #2
        sta     VBLANK
        lda     #0
        sta     APERR
        sta     APSTEP
        sta     DIRSEL

        jsr     FNARM
        jsr     FNCHK
        beq     HAVE
        lda     #FNENOC
        sta     APERR
        jmp     FAIL

HAVE:   jsr     MHOST
        bne     FAIL
        jsr     DRAW            ; first listing
        bne     FAIL

        lda     #0
        sta     VBLANK
        jsr     DINIT
        jmp     DLOOP

FAIL:   jsr     SHOWERR
        lda     #0
        sta     VBLANK
        jsr     DINIT
        jmp     DLOOP

; ---------------------------------------------------------------------------
; APPVBL -- one frame's worth of client work, called from the display loop
; while the screen is blanked.
APPVBL: jsr     INSCAN
        sta     APERR+0         ; reuse as scratch; APERR is only live on FAIL
        and     #IN_DOWN
        beq     AV1
        lda     DIRSEL
        clc
        adc     #1
        cmp     DIRCNT
        bcs     AVDONE          ; already on the last entry
        sta     DIRSEL
        jmp     AVREDRAW
AV1:    lda     APERR+0
        and     #IN_UP
        beq     AV2
        lda     DIRSEL
        beq     AVDONE          ; already at the top
        sec
        sbc     #1
        sta     DIRSEL
        jmp     AVREDRAW
AV2:    lda     APERR+0
        and     #IN_FIRE
        beq     AVDONE
        jmp     BOOTSEL
AVDONE: rts

AVREDRAW:
        lda     #2              ; blank while we talk; a listing is many
        sta     VBLANK          ;   round trips and the frame will not finish
        jsr     DRAW
        lda     #0
        sta     VBLANK
        rts

; ---------------------------------------------------------------------------
; MHOST -- MOUNT_HOST(HOSTSL). Z set on success.
MHOST:  lda     #FNDEVF
        sta     FNDEV
        lda     #FNCMHST
        sta     FNCMD
        lda     #1
        sta     FNNPR
        jsr     FNBEG
        lda     #HOSTSL
        jsr     FNPB
        jsr     FNGO
        sta     APERR
        cmp     #FNEOK
        bne     MHE
        jsr     FNACK
        sta     APERR
        cmp     #FNEOK
        beq     MHOK
MHE:    lda     #1
        sta     APSTEP
        lda     #1              ; Z clear
        rts
MHOK:   lda     #0
        rts

; ---------------------------------------------------------------------------
; ODIR -- OPEN_DIRECTORY(host, "/"). Z set on success.
;
; One parameter and then a FULL 256-byte payload: the path, its NUL, and then
; padding -- which the server reads as "no filter".
ODIR:   lda     #FNDEVF
        sta     FNDEV
        lda     #FNCODIR
        sta     FNCMD
        lda     #1
        sta     FNNPR
        jsr     FNBEG
        lda     #HOSTSL
        jsr     FNPB
        jsr     FNPBEG
        lda     #(TROOT)&$FF
        sta     FNPTRL
        lda     #(TROOT)>>8
        sta     FNPTRH
        jsr     FNPSTR
        jsr     FNPEND
        jsr     FNGO
        sta     APERR
        cmp     #FNEOK
        bne     ODE
        jsr     FNACK
        sta     APERR
        cmp     #FNEOK
        beq     ODOK
ODE:    lda     #2
        sta     APSTEP
        lda     #1
        rts
ODOK:   lda     #0
        rts

; ---------------------------------------------------------------------------
; RDENT -- READ_DIR_ENTRY(NAMELN, 0). Z set on success; the entry is left in
; the reply window and is NOT copied anywhere.
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
        lda     #0
        rts
RDE:    sta     APERR
        lda     #3
        sta     APSTEP
        lda     #1
        rts

; ---------------------------------------------------------------------------
; DRAW -- re-open the directory and paint the visible list. Z set on success.
DRAW:   jsr     ODIR
        bne     DRBAD

        lda     #(TTITLE)&$FF
        sta     FNPTRL
        lda     #(TTITLE)>>8
        sta     FNPTRH
        lda     #0
        jsr     FNRSTR

        lda     #0
        sta     DIRCNT
        ldx     #0              ; X = row index within the list
DRLOOP: stx     DIRWANT
        jsr     RDENT
        bne     DRBAD
        jsr     FNEOF
        beq     DREND           ; the $7F,$7F marker: stop, and do NOT read on
        ldx     DIRWANT

        ; Row: the cursor gutter, then the name straight from the reply.
        txa
        clc
        adc     #ROW0
        jsr     FNROWA
        lda     DIRWANT
        cmp     DIRSEL
        bne     DRNOCUR
        lda     #'>'
        jmp     DRCUR
DRNOCUR: lda    #' '
DRCUR:  sta     FNRSEL+FH_TCHR
        ldx     #0              ; reply offset: the name starts at 0
        ldy     #FNTCOL-1
        jsr     FNRRPX
        jsr     FNENDR

        ldx     DIRWANT
        inx
        stx     DIRCNT
        cpx     #NROWS
        bcc     DRLOOP

DREND:  ; blank any rows the listing did not fill
        ldx     DIRCNT
DRBL:   cpx     #NROWS
        bcs     DROK
        txa
        clc
        adc     #ROW0
        jsr     FNROWA
        jsr     FNENDR
        inx
        jmp     DRBL
DROK:   lda     #0
        rts
DRBAD:  lda     #1
        rts

; FNRRPX -- append up to Y characters of the reply window at offset X to the
; row being composed, stopping at NUL. (FNRRPL selects the row itself; this
; one does not, so a cursor character can go first.)
FNRRPX: lda     FNRPLY,x
        beq     FNRRX2
        sta     FNRSEL+FH_TCHR
        inx
        dey
        bne     FNRRPX
FNRRX2: rts

; ---------------------------------------------------------------------------
; BOOTSEL -- mount the selected entry and boot it.
;
; The name is not in RAM, so the directory is re-opened and re-read up to the
; selection, and the name is then streamed from the reply window straight into
; the path payload.
BOOTSEL:
        lda     #2
        sta     VBLANK
        jsr     ODIR
        beq     BSOPEN          ; branch range: invert and JMP
        jmp     BSBAD
BSOPEN:
        lda     #0
        sta     DIRWANT
BSSKIP: jsr     RDENT
        beq     BSSK1
        jmp     BSBAD
BSSK1:  jsr     FNEOF
        bne     BSSK2
        jmp     BSBAD           ; ran off the end: the selection is gone
BSSK2:
        lda     DIRWANT
        cmp     DIRSEL
        beq     BSGOT
        inc     DIRWANT
        jmp     BSSKIP

; ---- SET_DEVICE_FULLPATH(dev, host, mode, "/" + name) ----------------------
BSGOT:  lda     #FNDEVF
        sta     FNDEV
        lda     #FNCSDFP
        sta     FNCMD
        lda     #3
        sta     FNNPR
        jsr     FNBEG
        lda     #DEVSLOT
        jsr     FNPB
        lda     #HOSTSL
        jsr     FNPB
        lda     #FMREAD
        jsr     FNPB
        jsr     FNPBEG
        lda     #(TROOT)&$FF
        sta     FNPTRL
        lda     #(TROOT)>>8
        sta     FNPTRH
        jsr     FNPSTR          ; the "/" prefix
        ldx     #0
        ldy     #NAMELN
        jsr     FNPRPL          ; the name, cartridge to cartridge
        jsr     FNPEND
        jsr     FNGO
        sta     APERR
        cmp     #FNEOK
        bne     BSBAD2
        jsr     FNACK
        sta     APERR
        cmp     #FNEOK
        bne     BSBAD2

; ---- MOUNT_IMAGE(dev, mode) ------------------------------------------------
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
        sta     APERR
        cmp     #FNEOK
        bne     BSBAD2
        jsr     FNACK
        sta     APERR
        cmp     #FNEOK
        bne     BSBAD2

BSWAIT: ldy     #0
BSW1:   ldx     #0
BSW2:   lda     FNBST
        cmp     #FNBRDY
        beq     BSBOOT
        cmp     #FNBFAIL
        beq     BSBAD2
        dex
        bne     BSW2
        dey
        bne     BSW1
        lda     #FNEWAIT
        sta     APERR
BSBAD2: lda     #5
        sta     APSTEP
BSBAD:  jsr     SHOWERR
        lda     #0
        sta     VBLANK
        rts                     ; back to the display loop with the error up

BSBOOT: jsr     FNBLK
        jmp     FNSWAP          ; does not return

; ---------------------------------------------------------------------------
SHOWERR:
        lda     #(TFAIL)&$FF
        sta     FNPTRL
        lda     #(TFAIL)>>8
        sta     FNPTRH
        lda     #1
        jsr     FNRSTR
        lda     #2
        jsr     FNROWA
        lda     #'S'
        sta     FNRSEL+FH_TCHR
        lda     APSTEP
        jsr     FNHEX
        lda     #' '
        sta     FNRSEL+FH_TCHR
        lda     #'E'
        sta     FNRSEL+FH_TCHR
        lda     APERR
        jsr     FNHEX
        jsr     FNENDR
        rts

TTITLE: DB      "FUJINET DIR",0
TFAIL:  DB      "FAILED",0
TROOT:  DB      "/",0

        INCLUDE "fujilib.inc"
        INCLUDE "fujidisp.inc"

        ORG     $1FFC
        DW      START
        DW      START

        END
