; fujiboot.asm -- M2: mount a cartridge image over the network and boot it.
;
; MOUNT_HOST -> SET_DEVICE_FULLPATH -> MOUNT_IMAGE. The last one makes the
; ESP32 side stream the image back to the cartridge on the same link, addressed
; to the DBC device, while MOUNT_IMAGE's own reply is still outstanding; the
; cart stages it and reports BOOT_STATE.
;
; The boot itself: arm the swap (BOOTLOCK), then FNSWAP copies a stub into
; zero-page RAM and jumps to it. The stub stores to the swap hotspot -- the
; cart flips the window to the staged image between that store and the next
; fetch, which is safe because the next fetch is in RAM with A12 low -- and
; then JMPs through ($1FFC), the new image's own reset vector, exactly what the
; 6507 fetches at power-on.
;
; Target host slot and path come from bootcfg.inc, written by build.sh:
;   BOOT_HOST=0 BOOT_PATH=/vcsgame.bin ./build.sh fujiboot
;
; Exit test: emu/boottest.lua dumps the whole served 4K window after the swap
; and requires it to be byte-identical to the file on disk.

        CPU     6502
        INCLUDE "vcs.inc"

; ---------------- zero page ----------------
; $80-$A6 is the swap stub's landing ground (see FNSWAP), so nothing the
; client keeps may live there. The stack starts at $FF and must not descend
; past $A8.
PAD3    EQU     $81             ; the display kernel's 3-cycle pad target
SAVSP   EQU     $82             ; stack pointer, parked across the kernel
APERR   EQU     $8C             ; the failing code
APSTEP  EQU     $8D             ; which step failed

        INCLUDE "fujinet.inc"

DEVSLOT EQU     0

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

        lda     #2              ; blanked while we talk
        sta     VBLANK
        lda     #0
        sta     APERR
        sta     APSTEP

        jsr     FNARM
        jsr     FNCHK
        beq     HAVE
        lda     #FNENOC
        sta     APERR
        jmp     SHOW

; ---- MOUNT_HOST(host) ------------------------------------------------------
HAVE:   lda     #FNDEVF
        sta     FNDEV
        lda     #FNCMHST
        sta     FNCMD
        lda     #1
        sta     FNNPR
        jsr     FNBEG
        lda     #BOOTHST
        jsr     FNPB
        jsr     FNGO
        sta     APERR
        cmp     #FNEOK
        bne     ERR1
        jsr     FNACK
        sta     APERR
        cmp     #FNEOK
        beq     SDFP
ERR1:   lda     #1
        sta     APSTEP
        jmp     SHOW

; ---- SET_DEVICE_FULLPATH(dev, host, mode, path) ----------------------------
; Three one-byte parameters and then a FULL 256-byte NUL-padded path. The
; server reads exactly 256 and fails a short one.
SDFP:   lda     #FNDEVF
        sta     FNDEV
        lda     #FNCSDFP
        sta     FNCMD
        lda     #3
        sta     FNNPR
        jsr     FNBEG
        lda     #DEVSLOT
        jsr     FNPB
        lda     #BOOTHST
        jsr     FNPB
        lda     #FMREAD
        jsr     FNPB
        lda     #(BOOTPTH)&$FF
        sta     FNPTRL
        lda     #(BOOTPTH)>>8
        sta     FNPTRH
        jsr     FNPATH
        jsr     FNGO
        sta     APERR
        cmp     #FNEOK
        bne     ERR2
        jsr     FNACK
        sta     APERR
        cmp     #FNEOK
        beq     MIMG
ERR2:   lda     #2
        sta     APSTEP
        jmp     SHOW

; ---- MOUNT_IMAGE(dev, mode) ------------------------------------------------
; This is the one that takes real time: the ESP32 fetches the file and streams
; it back to the cartridge as DBC push frames while this reply is still
; outstanding. FNBEG's default timeout is deliberately generous.
MIMG:   lda     #FNDEVF
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
        bne     ERR3
        jsr     FNACK
        sta     APERR
        cmp     #FNEOK
        beq     WAITB
ERR3:   lda     #3
        sta     APSTEP
        jmp     SHOW

; ---- wait for the staged image ---------------------------------------------
WAITB:  ldy     #0
WAITB1: ldx     #0
WAITB2: lda     FNBST
        cmp     #FNBRDY
        beq     BOOT
        cmp     #FNBFAIL
        beq     BFAIL
        dex
        bne     WAITB2
        dey
        bne     WAITB1
        lda     #FNEWAIT        ; the image never arrived
        sta     APERR
        lda     #4
        sta     APSTEP
        jmp     SHOW
BFAIL:  lda     FNBER
        sta     APERR
        lda     #5
        sta     APSTEP
        jmp     SHOW

; ---- boot it ---------------------------------------------------------------
BOOT:   jsr     FNBLK           ; arm the swap
        jmp     FNSWAP          ; does not return

; ---- the failure screen ----------------------------------------------------
; As useful as the success one: it says WHICH step failed and with what, which
; is the difference between a bug report and a shrug.
SHOW:   lda     #(TTITLE)&$FF
        sta     FNPTRL
        lda     #(TTITLE)>>8
        sta     FNPTRH
        lda     #0
        jsr     FNRSTR

        lda     #(TFAIL)&$FF
        sta     FNPTRL
        lda     #(TFAIL)>>8
        sta     FNPTRH
        lda     #2
        jsr     FNRSTR

        lda     #4
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

        lda     #0
        sta     VBLANK
        jsr     DINIT
        jmp     DLOOP

TTITLE: DB      "FUJIBOOT",0
TFAIL:  DB      "FAILED",0

        INCLUDE "bootcfg.inc"
        INCLUDE "fujilib.inc"
APPVBL: rts                     ; this client needs no per-frame work

        INCLUDE "fujidisp.inc"

        ORG     $1FFC
        DW      START
        DW      START

        END
