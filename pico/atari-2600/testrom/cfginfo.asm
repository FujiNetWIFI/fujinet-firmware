; cfginfo.asm -- CONFIG bank 2: what the adapter says about itself.
;
; GET_ADAPTERCONFIG_EXTENDED, rendered straight out of the reply window. The
; reply is 240 bytes and the window is 512, so it lands in slice 0 whole and
; every field a client wants is below offset 256 -- which is why a single-byte
; index reaches all of them.
;
; Any button goes back to the host screen.

        CPU     6502
        INCLUDE "vcs.inc"

PAD3    EQU     $81
SAVSP   EQU     $82

        INCLUDE "fujinet.inc"
        INCLUDE "cfgdefs.inc"

; Field offsets inside the packed AdapterConfigExtended, from
; lib/device/fujiDevice/fujiDevice.h.
AC_SSID EQU     0               ; char[33]
AC_HOST EQU     33              ; char[64]
AC_VER  EQU     125             ; char[15]
AC_SIP  EQU     140             ; char[16], already formatted as text
AC_SGW  EQU     156             ; char[16]
AC_SMSK EQU     172             ; char[16]
AC_SDNS EQU     188             ; char[16]
AC_SMAC EQU     204             ; char[18]

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
        and     #IN_FIRE|IN_SEL
        beq     AVDONE
        lda     #BANKHST
        jmp     CFGOTO          ; does not return
AVDONE: rts

; ---------------------------------------------------------------------------
DRAW:   lda     #(TTITLE)&$FF
        sta     FNPTRL
        lda     #(TTITLE)>>8
        sta     FNPTRH
        lda     #0
        jsr     FNRSTR

        lda     #FNDEVF
        sta     FNDEV
        lda     #FNCADPX
        sta     FNCMD
        lda     #0
        sta     FNNPR
        jsr     FNBEG
        jsr     FNGO
        sta     CFERR
        cmp     #FNEOK
        bne     DRBAD
        jsr     FNACK
        sta     CFERR
        cmp     #FNEOK
        bne     DRBAD

        ; Twelve columns will not hold a label and a value on one row, so each
        ; field gets a label row and a value row. Eight fields, sixteen rows,
        ; and the screen is twenty-one.
        ldx     #0              ; index into FIELDS
        ldy     #ROW0           ; the row being written
DRF:    sty     CFWANT
        lda     FIELDS,x
        cmp     #$FF
        beq     DRDONE
        sta     CFSEL           ; the reply offset, borrowed as scratch
        inx
        stx     CFCNT

        ; The label.
        lda     LBLLO-1,x
        sta     FNPTRL
        lda     LBLHI-1,x
        sta     FNPTRH
        lda     CFWANT
        jsr     FNRSTR

        ; The value, straight from the reply window.
        lda     CFWANT
        clc
        adc     #1
        pha
        ldx     CFSEL
        ldy     #FNTCOL
        pla
        jsr     FNRRPL

        ldx     CFCNT
        lda     CFWANT
        clc
        adc     #2
        tay
        cpy     #ROW0+18
        bcc     DRF
DRDONE: lda     #(THINT)&$FF
        sta     FNPTRL
        lda     #(THINT)>>8
        sta     FNPTRH
        lda     #ROWERR+1
        jsr     FNRSTR
        rts

DRBAD:  lda     #1
        sta     CFSTEP
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

; The fields, as reply offsets, terminated by $FF. Every one is below 256, so
; the offset is a byte and FNRRPL can reach it with X alone.
FIELDS: DB      AC_SSID, AC_SIP, AC_SGW, AC_SDNS, AC_SMAC, AC_VER, $FF

LBLLO:  DB      (LSSID)&$FF, (LIP)&$FF, (LGW)&$FF, (LDNS)&$FF
        DB      (LMAC)&$FF, (LVER)&$FF
LBLHI:  DB      (LSSID)>>8, (LIP)>>8, (LGW)>>8, (LDNS)>>8
        DB      (LMAC)>>8, (LVER)>>8

LSSID:  DB      "SSID",0
LIP:    DB      "IP",0
LGW:    DB      "GATEWAY",0
LDNS:   DB      "DNS",0
LMAC:   DB      "MAC",0
LVER:   DB      "VERSION",0
TTITLE: DB      "FN INFO",0
THINT:  DB      "FIRE=BACK",0

        INCLUDE "fujilib.inc"
        INCLUDE "fujidisp.inc"

        END
