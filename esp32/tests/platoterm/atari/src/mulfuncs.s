
	;; Multiply functions, thanks to Christian Groessler

.include "zeropage.inc"
.export _mul0625, _mul0375
.code

; unsigned int __fastcall__ mul0625(unsigned int val);
; 0.625x = (1/2 + 1/8)x

_mul0625:
	stx ptr1+1

	asl	; double it
	rol ptr1+1
	
	sta ptr2	; save * 2
	ldy ptr1+1
	
	asl	; calculate * 8
	rol ptr1+1
	asl
	rol ptr1+1
	
	clc	; add to * 2
	adc ptr2
	sta ptr2
	tya
	adc ptr1+1

	lsr 	; now divide by 16
	ror ptr2
	lsr
	ror ptr2
	lsr
	ror ptr2
	lsr
	ror ptr2
	
	tax
	lda ptr2
	rts

; unsigned int __fastcall__ mul0375(unsigned int val);
; 0.375x = (1/4 + 1/8)x

_mul0375:
	sta ptr2	; save original value
	stx ptr2+1
	stx ptr1+1	; msb of shifted value

	asl	; double it
	rol ptr1+1
	
	clc	; get * 3
	adc ptr2
	sta ptr2
	
	lda ptr1+1
	adc ptr2+1

	lsr 	; now divide by 8
	ror ptr2
	lsr
	ror ptr2
	lsr
	ror ptr2
	
	tax
	lda ptr2
	rts
.end
	
.endif
