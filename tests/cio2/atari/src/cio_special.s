	;; CIO Open for N:

	.include "atari.inc"

	.import _ret
	.import _err
	.import __cio_special
	.export _cio_special

_cio_special:
	jsr __cio_special
	lda _ret
	ldy _err
	rts
	
