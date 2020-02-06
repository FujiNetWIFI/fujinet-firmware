;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;
;;;
;;; rel.s
;;; example of routines to be moved as a package.
;;;
;;; _reloc_begin & _reloc_end mark the region of 
;;; memory to be moved.
;;;
;;; _reloc_code_begin is the beginning of the
;;; code to be scanned for address fixups.
;;;

	.export _reloc_begin
	.export _reloc_code_begin
	.export _function1
	.export _function2
	.export _function3
	.export _reloc_end
	
	.code


;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;
;;;
;;; _reloc_begin is where the relocator starts
;;; working from.
;;; 
_reloc_begin:

;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;
;;;
;;; public function table.  this table needs to
;;; reside at the beginning of the region to be
;;; remapped.  following the function table the
;;; programmer is free to declare data elements
;;; used by the public/private functions.
;;;
;;; the format is as follows:
;;;
;;;    # of functions (byte)
;;;    function1 (word)
;;;    function2 (word)
;;;    functionN (word)
;;; 
_functab:
	.byte 3			; number of public functions exposed via USR()
	.word _function1
	.word _function2
	.word _function3

_data_table:
	.byte 1			; data to be returned to basic USR()
	.byte 2			; ...ditto for function 2
	.byte 4			; ...ditto for function 3

;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;
;;;
;;; _reloc_code_begin is where the relocator scans
;;; for instruction remapping - right now it only
;;; handles 3 byte instructions.
;;; 
_reloc_code_begin:
	
_function1:
	pla			; pull length byte
	ldy	#$00
	lda	_data_table,y
	sta	$d4
	lda	#$00
	sta	$d4+1
	rts
	
_function2:
	pla			; pull length byte
	ldy	#$01
	lda	_data_table,y
	sta	$d4
	lda	#$00
	sta	$d4+1
	rts
	
_function3:
	pla			; pull length byte
	jsr	privatefunction	; call private function...
	rts

;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;
;;;
;;; declare a private function used by code that is
;;; relocated.
;;; 
privatefunction:
	ldy	#$02
	lda	_data_table,y
	sta	$d4
	lda	#$00
	sta	$d4+1
	rts

;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;
;;;
;;; _reloc_end is where the relocator stops working
;;; 
_reloc_end:

	.end
	

	

