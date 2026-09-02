; fujistub.inc -- the cartridge's half of the mailbox page, plus image padding.
; $F00-$F1F belongs to the client; $F20-$FFF is written by the cartridge.

    org 0F00h
fn_get:                     ; A = offset in page $F00 -> A = byte there
    movp a,@a
    ret

    org 0FFFh               ; pad out to a full 3K cart image
    db 0
