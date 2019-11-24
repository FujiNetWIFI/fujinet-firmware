	;; CIO Open for N:

	.include "atari.inc"

	.import _ret
	.import _err
	.import __cio_close
	.export _cio_close
	
_cio_close:
	jsr __cio_close
	lda _ret
	ldy _err
	rts
	
