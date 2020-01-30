	;; Call SIO

	.export _siov
	.export _intr
	.import _trip
	
_siov:	JSR $E459
	RTS
	
_intr:	PLA
	LDA #$01
	STA _trip
	RTI
