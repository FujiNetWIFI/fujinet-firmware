	;; CIO Open for N:

	.include "atari.inc"

	.import _ret
	.import _err
	.import _trip
	.import _zp_save, _zp_restore
	.import __cio_open, __cio_close, __cio_get, __cio_put, __cio_status, __cio_special
	.export _cio_open, _cio_close, _cio_get, _cio_put, _cio_status, _cio_special, _intr

_intr:	LDA #$01
	STA _trip
	PLA
	RTI
	
_cio_close:
	jsr _zp_save
	jsr __cio_close
	jsr _zp_restore
	lda _ret
	ldy _err
	rts
	
_cio_get:
	jsr _zp_save
	jsr __cio_get
	jsr _zp_restore
	lda _ret
	ldy _err
	rts
	
_cio_open:
	jsr _zp_save
	jsr __cio_open
	jsr _zp_restore
	lda _ret
	ldy _err
	rts
	
_cio_put:
	jsr _zp_save
	sta _ret
	jsr __cio_put
	jsr _zp_restore
	lda _ret
	ldy _err
	rts
	
_cio_special:
	jsr _zp_save
	jsr __cio_special
	jsr _zp_restore
	lda _ret
	ldy _err
	rts
	
_cio_status:
	jsr _zp_save
	jsr __cio_status
	jsr _zp_restore
	lda _ret
	ldy _err
	rts
	
