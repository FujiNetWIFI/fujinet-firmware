; input.inc -- the two Studio II keypads.
;
; OUT 2 latches a key number (0-9, the low nibble of M(R(X))) into the
; console's CD4515; EF3 then reads that key on pad A, EF4 on pad B. OUT is a
; memory READ of M(R(X)), so R(X) points at console RAM (V_KTMP), never at
; the cart. FCALLed; may clobber R8, R9.

KEY_DELAY       EQU     24      ; frames before a held key repeats
KEY_RATE        EQU     6       ; frames between repeats

; in_scan -- D = the key held now: KEY_A + k, KEY_B + k, or 0.
        FIT     34
in_scan:
        LDR     R9, V_KTMP
        LDI     0
        PLO     R8
isc1:   GLO     R8
        STR     R9
        SEX     R9
        OUT     2                       ; R9++
        DEC     R9
        SEX     R2
        B3      isc2
        B4      isc3
        INC     R8
        GLO     R8
        XRI     10
        BNZ     isc1
        FRET                            ; D = 0
isc2:   GLO     R8
        ORI     KEY_A
        FRET
isc3:   GLO     R8
        ORI     KEY_B
        FRET

; in_get -- D = a key just pressed, or held long enough to repeat; else 0.
        FIT     48
in_get: FCALL   in_scan
        PHI     R8
        LDR     R9, V_KLAST
        GHI     R8
        SEX     R9
        XOR
        SEX     R2
        BZ      ign2                    ; the same as last scan
        GHI     R8
        STR     R9                      ; a change: a press (or a release)
        INC     R9
        GLO     RE
        ADI     KEY_DELAY
        STR     R9                      ; V_KTIME
        GHI     R8
        FRET
ign2:   GHI     R8
        BZ      ign4                    ; nothing held
        INC     R9
        LDN     R9                      ; repeat when RE.0 reaches V_KTIME
        STR     R2
        GLO     RE
        SM                              ; RE.0 - V_KTIME, signed
        ANI     80H
        BNZ     ign4                    ; not yet
        GLO     RE
        ADI     KEY_RATE
        STR     R9
        GHI     R8
        FRET
ign4:   LDI     0
        FRET
