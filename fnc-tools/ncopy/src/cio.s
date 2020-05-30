	;; Call CIO

	.export _ciov, _dciov

_ciov:	LDX #$00
	JSR $E456
	RTS
	
_dciov:	LDX #$20
	JSR $E456
	RTS
