	.include "atari.inc"

	.import _ret
	.import _err
	.import __cio_open, __cio_close, __cio_get, __cio_put, __cio_status, __cio_special
	.export _cio_open, _cio_close, _cio_get, _cio_put, _cio_status, _cio_special

_cio_close:
	jsr __cio_close
	lda _ret
	ldy _err
	rts
	
_cio_get:
	jsr __cio_get
	lda _ret
	ldy _err
	rts
	
_cio_open:
	jsr __cio_open
	lda _ret
	ldy _err
	rts
	
_cio_put:
	sta _ret
	jsr __cio_put
	lda _ret
	ldy _err
	rts
	
_cio_special:
	jsr __cio_special
	lda _ret
	ldy _err
	rts
	
_cio_status:
	jsr __cio_status
	lda _ret
	ldy _err
	rts
	
