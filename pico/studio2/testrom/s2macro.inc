; s2macro.inc -- macros and RAM variables for Studio II FujiNet clients.
; Include after fujinet.inc and before any ORG.

; ---- SCRT: calls, returns and far jumps (s2call.inc) ----
; No LBR anywhere in a client: a 3-cycle instruction while the 1861 is
; fetching costs a byte of the line (tools/checkrom.py). Leave a page with
; FJMP, or call into it.
FCALL   MACRO   adr
        SEP     R4
        DW      adr
        ENDM

FRET    MACRO
        SEP     R5
        ENDM

FJMP    MACRO   adr
        SEP     R7
        DW      adr
        ENDM

; Start the next page unless `size` bytes still fit in this one. Every
; routine with a short branch is preceded by one: a short branch reaches
; only its own page (AS checks; the size is measured, not derived).
FIT     MACRO   size
        IF      ((($) & 0FFH) + (size)) > 100H
        ORG     (($) + 0FFH) & 0FF00H
        ENDIF
        ENDM

; R = 16-bit constant.
LDR     MACRO   r, val
        LDI     HI(val)
        PHI     r
        LDI     LO(val)
        PLO     r
        ENDM

; One hotspot read at page:D, RF parked again after. Clobbers D.
HOT     MACRO   page
        PLO     RF
        LDI     page
        PHI     RF
        LDN     RF
        LDI     FN_PARK
        PHI     RF
        ENDM

; D = the cart byte at adr (status page, reply window). RF parked after.
PEEK    MACRO   adr
        LDI     LO(adr)
        PLO     RF
        LDI     HI(adr)
        PHI     RF
        LDN     RF
        PHI     RD
        LDI     FN_PARK
        PHI     RF
        GHI     RD
        ENDM

; The claim that keeps the mailbox live: last, at $07FC.
S2CLAIM MACRO
        ORG     FN_CLAIM
        DB      "FUJI"
        ENDM

; ---- RAM variables of s2call.inc / fujilib.inc / input.inc ----
V_TMO   EQU     RAMVARS+00H     ; fn_go's timeout, seconds
V_SEQ   EQU     RAMVARS+01H     ; the sequence number fn_go launched
V_CNT   EQU     RAMVARS+02H     ; fn_frames countdown, 16-bit
V_LAST  EQU     RAMVARS+04H     ; RE.0 when fn_tick last looked
V_KTMP  EQU     RAMVARS+05H     ; the key OUT 2 latches
V_KLAST EQU     RAMVARS+06H     ; input.inc: the key held at the last scan
V_KTIME EQU     RAMVARS+07H     ; input.inc: RE.0 when it auto-repeats next
V_T     EQU     RAMVARS+08H     ; scratch for one routine at a time
V_APP   EQU     RAMVARS+10H     ; the client's own variables start here
