	;; Call CIO

	.export _ciov

_ciov:	LDX #$00
	JSR $E456
	RTS
	
