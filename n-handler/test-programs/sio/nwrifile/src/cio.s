	;; Call CIO

	.export _ciov

_ciov:	TAX
	JSR $E456
	TYA
	RTS
	
