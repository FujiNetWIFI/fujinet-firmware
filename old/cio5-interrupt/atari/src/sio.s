	;; Call SIO

	.export _siov
	.export _inter
	.import _trip
	
_siov:	JSR $E459
	RTS

	;; Inter routine
_inter:	PLA
	LDA #$01
	STA _trip
	RTI
