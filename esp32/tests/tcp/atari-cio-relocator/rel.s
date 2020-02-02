;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;
;;;
;;; rel.s
;;; example of routines to be moved as a package.
;;;
;;; _reloc_begin & _reloc_end mark the region of 
;;; memory to be moved.
;;;

	.export _reloc_begin
	.export _function1
	.export _function2
	.export _function3
	.export _reloc_end
	
	.code

_reloc_begin:
	
_functab:
	.word _function1
	.word _function2
	.word _function3

_function1:
	pla			; pull length byte
	lda	#$01
	sta	$d4
	lda	#$00
	sta	$d4+1
	rts
	
_function2:
	pla			; pull length byte
	lda	#$02
	sta	$d4
	lda	#$00
	sta	$d4+1
	rts
	
_function3:
	pla			; pull length byte
	jmp	_return4instead
	lda	#$03
	sta	$d4
	lda	#$00
_return4instead:
	jsr	privatefunction
	rts

privatefunction:
	lda	#$04
	sta	$d4
	lda	#$00
	sta	$d4+1
	rts
	
_reloc_end:

	.end
	

	

