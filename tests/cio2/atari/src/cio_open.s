	;; CIO Open for N:

	.include "atari.inc"

	.import _ret
	.import _err
	.import __cio_open
	.export _cio_open

_cio_open:
	jsr __cio_open
	lda _ret
	ldy _err
	rts
	
