; corpus.asm -- the program inside tools/mkcorpus.py's synthetic images: a
; plain Studio II cart (no FujiNet) that copies eight bytes from each of
; $0A00, $0B00, $0C00, $0D00, $0E00 and $0F00 into the BIOS's display RAM, so
; which pages the cart serves -- and with what -- is on screen for Tier A.
; Keeps to the BIOS's conventions: its ISR owns R0, R1, R2, R8, R9 and RB.

        CPU     1802
        INCLUDE "fujinet.inc"

        ORG     0400H
        DB      04H, 02H                ; byte-code: call native $0402
        LDI     06H                     ; RA.0: pages left
        PLO     RA
        LDI     0AH                     ; RC.1: the page to read
        PHI     RC
        LDI     09H                     ; RD: display RAM, a row of 8 bytes...
        PHI     RD
        LDI     10H                     ; ...every 16 bytes: 4 rows apart
        PLO     RD
page:   LDI     0
        PLO     RC
        LDI     8
        PLO     RE
copy:   LDA     RC
        STR     RD
        INC     RD
        DEC     RE
        GLO     RE
        BNZ     copy
        GLO     RD                      ; next display row group
        ADI     18H
        PLO     RD
        GHI     RC
        ADI     1
        PHI     RC
        DEC     RA
        GLO     RA
        BNZ     page
spin:   BR      spin
