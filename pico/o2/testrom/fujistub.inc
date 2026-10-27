; fujistub.inc -- the cartridge's half of the mailbox page, plus image padding.
;
; $F00-$F1F belongs to the client; $F20-$FFF is written by the cartridge (the
; status block, then the 208-byte reply slice at $F30). fn_get has to live in
; this page because MOVP reads the page the PC is in.

    org 0F00h
fn_get:                     ; A = offset in page $F00 -> A = byte there
    movp a,@a
    ret

; The FUJI claim (see FN_R_CLAIM in fuji_mailbox.h). Without it the cartridge
; drops the mailbox at boot and a client reads NO CART.
    org 0F2Ch
    db "FUJI"

    org 0FFFh               ; pad out to a full 3K cart image
    db 0
