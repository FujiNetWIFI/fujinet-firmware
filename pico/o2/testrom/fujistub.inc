; fujistub.inc -- the cartridge's half of the mailbox page, plus image padding.
;
; $F00-$F1F belongs to the client; $F20-$FFF is written by the cartridge (the
; status block, then the 208-byte reply slice at $F30). fn_get has to live in
; this page because MOVP reads the page the PC is in.

    org 0F00h
fn_get:                     ; A = offset in page $F00 -> A = byte there
    movp a,@a
    ret

; $F2C-$F2F is the one part of the page the cartridge never publishes over, so
; it is where an image declares itself FujiNet-aware: this cart reserves
; $F20-$FFF for the mailbox and does not drive The Voice through $E0-$E3.
; Without the signature the cartridge disables the mailbox when this image is
; booted -- right for an ordinary game, fatal for a client, which comes up
; reading NO CART because the magic at $F29 was never republished.
    org 0F2Ch
    db "FUJI"

    org 0FFFh               ; pad out to a full 3K cart image
    db 0
