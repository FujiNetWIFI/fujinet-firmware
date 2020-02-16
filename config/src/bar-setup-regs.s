	;; bar setup code, sets up registers because grrrr

	.include "atari.inc"

	.export _bar_setup_regs

_bar_setup_regs:
	;; set prior
	lda #$08
	sta 623
	
	;; set color
	lda #$44
	sta $02C0
	sta $02C1
	sta $02C2
	sta $02C3
	sta 559			; Go ahead and turn off SDMCTL

	lda #$7C		; $7C00
	sta $D407		; to pmbase

	lda #$03		; players/missiles
	sta $D01D		; to GRACTL

	lda #$FF		; This is a shortcut.
	sta $D008		; same value into sizep/sizem
	sta $D009		; for quad size players/missiles
	sta $D00A
	sta $D00B
	sta $D00C

	lda #48			; set positions
	sta $D000
	lda #80
	sta $D001
	lda #112
	sta $D002
	lda #144
	sta $D003
	lda #176
	sta $D004
	lda #184
	sta $D005
	lda #192
	sta $D006
	lda #200
	sta $D007

	lda #$2E
	sta 559			; turn DMA on
	rts			; and go back.
