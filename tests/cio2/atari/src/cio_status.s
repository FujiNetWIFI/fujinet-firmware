	;; CIO Open for N:

	.include "atari.inc"

	.import _ret
	.import _err
	.import __cio_status
	.export _cio_status

_cio_status:
	jsr __cio_status
	lda _ret
	ldy _err
	rts
	
