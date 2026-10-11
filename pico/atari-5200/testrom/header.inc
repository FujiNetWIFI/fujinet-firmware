; header.inc -- the claim and the BIOS's cartridge header, for every client.
; $BFFD = $FF sends the BIOS straight to the entry vector, no title screen.

        .import start

        .segment "CLAIM"
        .byte   "FUJI"
        .byte   1               ; claim version
        .byte   0               ; mapper: from the size
        .byte   0               ; flags
        .byte   $02             ; $BFE7: PAL-compatible, for the 2-port BIOS

        .segment "HEADER"
        .byte   "FUJINET             "      ; $BFE8: 20 characters, unseen
        .byte   $FF, $FF                    ; $BFFC: year; $BFFD = $FF: no title
        .word   start                       ; $BFFE: entry
