	.list off
	.include "atari.inc"

	.export start

	.segment "CODE"

start:	.byte $00,$03,$00,$05,$C0,$E4
	lda #$70
	sta $0300
	lda #$01
	sta $0301
	lda #$26
	sta $0302
	lda #$40
	sta $0303
	lda #$00
	sta $0304
	lda #$1D
	sta $0305
	lda #$0F
	sta $0306
	lda #$00
	sta $0307
	lda #$8F
	sta $0308
	lda #$05
	sta $0309
	lda #$00
	sta $030A
	sta $030B
	jsr $E459
	jsr $1D01
	lda #$00
	sta $0244 		; coldst
	clc
	jmp ($000C)
	rts
