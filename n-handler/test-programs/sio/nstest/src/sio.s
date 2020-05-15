	;; Call SIO

	.export _siov
	.export _prcd
	.import _trip
	
_prcd:	LDA #$01
	STA _trip
	PLA
	RTI
	
_siov:	JSR $E459
	RTS
	
