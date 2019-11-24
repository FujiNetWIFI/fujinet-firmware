	;; CIO Open for N:

	.include "atari.inc"

	.import _ret
	.import _err
	.import __cio_put
	.export _cio_put

_cio_put:
	jsr __cio_put
	lda _ret
	ldy _err
	rts
	
