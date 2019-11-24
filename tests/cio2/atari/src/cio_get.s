	;; CIO Open for N:

	.include "atari.inc"

	.import _ret
	.import _err
	.import __cio_get
	.export _cio_get
	
_cio_get:
	jsr __cio_get
	lda _ret
	ldy _err
	rts
	
