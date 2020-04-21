	.export _ih
	.import _trip

_ih:	LDX #$01
	STA _trip
	PLA
	RTI
