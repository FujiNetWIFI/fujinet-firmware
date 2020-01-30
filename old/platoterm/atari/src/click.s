	;; Simple routine for keyboard click

.include "atari.inc"
.export	_click

.code
	
_click:	LDX	#$7F
click1:	STX	53279
	STX	WSYNC
	DEX
	BPL	click1
	RTS
	
