.include "atari.inc"
	
.export _screen_beep

.code
_screen_beep:
	lda #$00
	sta SDMCTL
	ldx #$06
	ldy #$FF
screen_beep1:	
	sty CONSOL
	sty WSYNC
	sty WSYNC
	dey
	bne screen_beep1
	dex
	bne screen_beep1
	lda #$3E
	sta SDMCTL
	rts
	
