	;; Call CIO

	.export _ciov, _ciov5

_ciov:	LDX #$00
	JSR $E456
	TYA
	RTS

_ciov5:	LDX #$05
	JSR $E456
	TYA
	RTS

