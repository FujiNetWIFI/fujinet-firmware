;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;
;;;
;;; driver.s
;;;
;;; loadable fuji driver
;;;

	;; used by the C code to locate all the bits to move, the
	;; bits to relocate, and the bits to ignore.
	.export _reloc_begin
	.export _reloc_code_begin
	.export _reloc_end

	D4      = $d4		; zero page addy used for indexing
	
	;; equates
	HATABS  = $031A		; where Atari's HATABS resides
	SAVMSC  = $58
	ROWCRS  = $54
	COLCRS  = $55

	
	;; DCB block
	DDEVIC  = $0300		; sio device
	DUNIT   = $0301		; sio unit number
	DCOMND  = $0302 	; sio command
	DSTATS  = $0303		; sio data direction
	DBUFLO  = $0304		; buffer to send
	DBUFHI  = $0305		; buffer to send
	DTIMLO  = $0306		; sio timeout
	DTIMHI  = $0307		; unused
	DBYTLO  = $0308		; buffer length
	DBYTHI  = $0309		; spare byte
	DAUX1  	= $030A		; 1st aux byte
	DAUX2	= $030B		; 2nd aux byte

	;; ZIOCB (zeropage) block
	ZDEVIC  = $20		; head of ZIOCB block (handler)
	ZUNIT   = $21		; ZIOCB drive
	ZCOMND  = $22		; ZIOCB command
	ZSTATS  = $23		; ZIOCB status
	ZBUFLO  = $24		; ZIOCB buffer lo
	ZBUFHI  = $25		; ZIOCB buffer hi
	ZPBLO   = $26		; ZIOCB put byte routine lo
	ZPBHI   = $27		; ZIOCB put byte routine hi
	ZBYTLO  = $28		; ZIOCB buffer lo
	ZBYTHI  = $29		; ZIOCB buffer hi
	ZAUX1   = $2a		; ZIOCB aux byte 1
	ZAUX2   = $2b		; ZIOCB aux byte 2
	ZAUX3   = $2c		; ZIOCB aux byte 3
	ZAUX4   = $2d		; ZIOCB aux byte 4
	ZSPARE  = $2f		; ZIOCB spare byte

	DVSTAT0			= $02ea		; device error & command status byte
	DVSTAT1 		= $02eb		; device status byte
	DVSTAT2 		= $02ec		; maximum device timeout in seconds
	DVSTAT3 		= $02ed		; number of bytes in output buffer

	;; 
	;; from sio.h
	;; 
	NDEV           		= $70	; atariwifi device number
	DSTATS_NONE    		= $00
	DSTATS_READ    		= $40
	DSTATS_WRITE   		= $80
	DTIMLO_DEFAULT 		= $0f	; 15 seconds
	DBYT_NONE      		= $00
	DBYT_OPEN      		= $ff

	;; 
	;; FujiNET return values
	;; 
	FUJI_DEAD      		= $00	; FN not responding
	FUJI_OK	       		= $03	; FN good to go
	FUJI_FULL_TAB  		= $02	; HATABS is full (unlikely)

	;;
	;; CIO SPECIAL commands
	;;
	SPECIAL_FLUSH		= 18

	;;
	;; functab indexes
	;; 
	FUNCTION_INIT        	= 1
	FUNCTION_CIO_OPEN    	= 3
	FUNCTION_CIO_CLOSE   	= 5
	FUNCTION_CIO_GET     	= 7
	FUNCTION_CIO_PUT     	= 9
	FUNCTION_CIO_STATUS  	= 11
	FUNCTION_CIO_SPECIAL 	= 13
	FUNCTION_CIO_VECTOR  	= 15
	VARIABLE_NTAB        	= 17
	VARIABLE_STATUS      	= 19
	VARIABLE_DVSTAT      	= 21
	VARIABLE_INBUF       	= 23
	VARIABLE_OUTBUF      	= 25
	VARIABLE_IINDEX      	= 29
	VARIABLE_INBUFLEN    	= 31
	MESSAGE_OPEN         	= 35
	
	.code
	
_reloc_begin:

functab:
	.byte 17		; (0) exposed addresses - these are remapped
	.addr init		; (1) driver init function
	.addr cio_open - 1	; (3) cio stuff
	.addr cio_close - 1	; (5)
	.addr cio_get - 1	; (7)
	.addr cio_put - 1	; (9)
	.addr cio_status - 1	; (11)
	.addr cio_special - 1	; (13)
	.addr cio_vector	; (15)
	.addr ntab		; (17)
	.addr status		; (19)
	.addr dvstatb		; (21)
	.addr inbuf		; (23)
	.addr outbuf		; (25)
	.addr oindex		; (27)
	.addr iindex		; (29) [7748]
	.addr inbuflen		; (31) [7749]
	.addr outbuflen		; (33) [8006]

NDEBUG:	 .byte $ff

status:	.byte 0			; status == FUJI_OK if we're init'd and all good

cioerr:	.byte 0
cioret:	.byte 0

aux1:	.byte 0
aux2:	.byte 0

tempb:	.byte 0
tempw:	.word 0			; temp word data

ntab:	.addr 0			; open
	.addr 0			; close
	.addr 0			; read
	.addr 0			; write
	.addr 0			; status
	.addr 0			; special
	.addr 0			; vector

aux1sv: .res 8, $00
aux2sv: .res 8, $00
inbuf:	.res $ff, $00		; input buffer
iindex:	.byte $00		; what character are we resting on in the input buffer?
inbuflen:
	.byte $00
outbuf:	.res $ff, $00		; output buffer
oindex:	.byte $00		; what character are we resting on in the output buffer?
outbuflen:
	.byte $00

dvstatb:
	.byte 0
	.byte 0
	.byte 0
	.byte 0

_reloc_code_begin:	

	
getvar:	
	lda	functab,x
	sta	tempw
	lda	functab+1,x
	sta	tempw+1
	rts
	
aux_save:
	ldy	ZUNIT
	lda	ZAUX1
	sta	aux1sv,y
	lda	ZAUX2
	sta	aux2sv,y
	rts

aux_load:
	ldy	ZUNIT
	lda	aux1sv,y
	sta	ZAUX1
	lda	aux2sv,y
	sta	ZAUX2
	rts

	
;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;
;;;
;;; default_cio_values
;;;
;;; load up a CIO command frame with defaults
;;; 
set_default_cio_values:
	lda	#NDEV
	sta	DDEVIC
	lda	#DTIMLO_DEFAULT
	sta	DTIMLO
	lda	#$00
	sta	DTIMHI
	lda	#$00
	sta	DTIMHI
	lda	ZUNIT
	sta	DUNIT
	rts

get_cio_return_values:
	lda	cioret
	ldy	cioerr
	rts

set_cio_return_values:
	sta	cioret
	sty	cioerr
	rts

.ifdef NDEBUG
	
set_visual:
	ldy	#0
	sta	(SAVMSC),y
	rts

clr_visual:
	pha
	tya
	pha
	lda	#' '
	ldy	#0
	sta	(SAVMSC),y
	iny
	sta	(SAVMSC),y
	pla
	tay
	pla
	rts

.macro	clr_vis
	.if	.defined( NDEBUG )
	jsr	clr_visual
	.endif
.endmacro
	
.macro	set_vis	code1, code2
	.if	.defined( NDEBUG )
	pha
	tya
	pha
	lda	#code1
	ldy	#0
	sta	(SAVMSC),y
	iny
	lda	#code2
	sta	(SAVMSC),y
	pla
	tay
	pla
	.endif
.endmacro

.endif
	
;;;
;;; (mostly good)
;;; 
cio_open:
	clr_vis
	set_vis	'o',' '
	jsr	wifi_hot
	cmp	#FUJI_OK
	beq	cio_open_continue
	lda	#146
	sta	cioerr
	lda	#146
	sta	cioret
	jsr	get_cio_return_values
	rts
cio_open_continue:
	clr_vis
	set_vis	'o', 'i'
	;; 
	jsr	aux_save
	;;
	set_vis	'o', 'p'
	lda	#NDEV
	sta	DDEVIC
	lda	#'o'
	sta     DCOMND
	lda	ZUNIT
	sta	DUNIT
	;;
	;; 	throwing data to fuji
	;; 
	lda	#DSTATS_WRITE
	sta	DSTATS
	;;
	;; 	whatever the caller passed us....
	;; 
	lda	#DBYT_OPEN
	sta	DBYTLO
	lda	#0
	sta	DBYTHI
	;; 
	;; 	pass user supplied string to fuji
	;;
	lda	ZBUFLO
	sta	DBUFLO
	lda	ZBUFHI
	sta	DBUFHI
	;;
	;; 	how long are we gonna wait? (15 seconds)
	;; 
	lda	#DTIMLO_DEFAULT
	sta	DTIMLO
	lda	#0
	sta	DTIMHI
	;;
	;; 	get aux data for this unit
	;; 
	lda	ZAUX1
	sta	DAUX1
	lda	ZAUX2
	sta	DAUX2
	;;
	;; 	make sio call....
	;;
	set_vis	'o', 'c'
	jsr	SIOV	
	bmi	cio_open_error
	;; 
	;; 	clear input and output buffer length
	;; 
	lda	#0
	sta	inbuflen
	sta	outbuflen
	sta	iindex
	sta	oindex
	;;
	;;
	;;
	set_vis 'o', 's'
	;;
	;; 	set CIO return values
	;;
	lda	#1
	sta	cioerr
	lda	#1
	sta	cioret
	jsr	get_cio_return_values
	rts
cio_open_error:
	set_vis	'o','e'
	lda	#170
	sta	cioerr
	sta	cioret
	jsr	get_cio_return_values
	rts

;;;
;;; (good) cio_close
;;; 
cio_close:
	jsr	wifi_hot
	cmp	#FUJI_OK
	beq	cio_close_continue
	lda	#146
	sta	cioerr
	lda	#0
	sta	cioret
	jsr	get_cio_return_values
	rts
cio_close_continue:	
	jsr	cio_put_flush
	;;
	;;
	;; 
	lda	#NDEV
	sta	ZDEVIC
	;;
	;;
	;; 
	lda	#'o'
	sta     DCOMND
	;;
	;;
	;; 
	lda	#DSTATS_NONE
	sta	DSTATS
	;;
	;;
	;; 
	lda	#$00
	sta	DBUFLO
	sta	DBUFHI
	;;
	;;
	;; 
	lda	#DBYT_NONE
	sta	DBYTLO
	sta	DBYTHI
	;;
	;;
	;; 
	lda	#DTIMLO_DEFAULT
	sta	DTIMLO
	;;
	;;
	;; 
	ldy	ZUNIT
	lda	aux1sv,y
	sta	DAUX1
	lda	aux2sv,y
	sta	DAUX2
	;;
	;;
	;; 
	jsr 	SIOV
	;;
	;;
	;; 
	ldy	ZUNIT
	lda	#$00
	sta	aux1sv,y
	sta	aux2sv,y
	;;
	;;
	;; 
	lda	#0
	sta	cioerr
	lda	#1
	sta	cioret
	jsr	get_cio_return_values
	rts

;;; 
;;; (good) cio_get
;;;
cio_get:
	jsr	wifi_hot
	cmp	#FUJI_OK
	beq	cio_get_continue
	lda	#136
	sta	cioerr
	sta	cioret
	jsr	get_cio_return_values
	rts
cio_get_continue:
	lda	inbuflen	; input buffer empty?
	beq	cio_get_check_fuji ; input buffer has nothing so hit fuji...
	lda	iindex
	cmp	inbuflen
	beq	cio_get_check_fuji ; if iindex == inbuflen
	jmp	cio_get_character  ; inbuflen > 0 and iindex != inbuflen
cio_get_check_fuji:
	lda	#0		; zap current input index and input buffer length
	sta	iindex
	sta	inbuflen
	jsr	cio_status	; ask fuji how many characters are waiting to be read....
	lda	DVSTAT0		; dvstat0 holds number of bytes in fuji waiting to read
	beq	cio_get_no_data	; nope - so bail
	lda	DVSTAT2		; dvstat2 holds connection status -- are we connected?
	beq	cio_get_no_data	; nope...we don't have a convo going so bail
	jmp	cio_get_siocall
cio_get_no_data:
	lda	#0
	sta	cioret
	lda	#170
	sta	cioerr
	jsr	get_cio_return_values
	rts
cio_get_siocall:
	lda	DVSTAT0		; get characters waiting to be read
	sta	inbuflen	; save accum (DVSTAT2) into input buffer length
	;;
	;;
	;; 
	lda	#NDEV
	sta	DDEVIC
	;;
	;;
	;; 
	lda	#'r'
	sta     DCOMND
	;;
	;;
	;; 
	lda	ZUNIT
	sta	DUNIT
	;;
	;;
	;; 
	lda	#DSTATS_READ
	sta	DSTATS
	;;
	;; 	get pointer to buffer - we just can't do inbuf/inbuf+1 here
	;;  	for some reason.
	;;
	ldx	#VARIABLE_INBUF
	jsr	getvar
	lda	tempw
	sta	DBUFLO
	lda	tempw+1
	sta	DBUFHI
	;;
	;;
	;;
	lda	inbuflen
	sta	DBYTLO
	lda	#0
	sta	DBYTHI
	;;
	;;
	;; 
	lda	#DTIMLO_DEFAULT
	sta	DTIMLO
	lda	#0
	sta	DTIMHI
	;;
	;;
	;; 
	lda	inbuflen
	sta	DAUX1
	lda	#0
	sta	DAUX2
	;;
	;;
	;; 
	jsr	SIOV
	bmi	cio_get_error
cio_get_character:		; iindex >= 0, inbuflen >= 0, inbuf has data....
	ldy	iindex		; nth character please
	lda	inbuf,y		; get it
	sta	cioret		; store it as the CIO return value
	iny			; increment index
	tya
	sta	iindex		; save new index
	lda	#1		; successful read
	sta	cioerr		; stuff it in cio error
	jsr	get_cio_return_values ; load regs based on cio* values
	rts
cio_get_error:
	lda	#136
	sta	cioerr
	sta	cioret
	jsr	get_cio_return_values
	rts


;;;
;;; (good)
;;; 
cio_put_flush:
	lda	outbuflen
	cmp	#0
	bne	cio_put_flush_continue
	rts
cio_put_flush_continue:
	lda	#NDEV
	sta	ZDEVIC
	;;
	;;
	;; 
	lda	#'w'
	sta     DCOMND
	;;
	;;
	;; 
	lda	#DSTATS_WRITE
	sta	DSTATS
	;;
	;;	pointer to output buffer
	;;
	ldx	#VARIABLE_OUTBUF
	jsr	getvar
	lda	tempw
	sta	DBUFLO
	lda	tempw+1
	sta	DBUFHI
	;;
	;;
	;; 
	lda	outbuflen
	sta	DBYTLO
	lda	#0
	sta	DBYTHI
	;;
	;;
	;; 
	lda	#DTIMLO_DEFAULT
	sta	DTIMLO
	;;
	;;
	;; 
	lda	outbuflen
	sta	DAUX1
	;;
	;;
	;; 
	lda	#0
	sta	DAUX2
	;;
	;;
	;; 
	jsr	SIOV
	;;
	;;
	;; 
	lda	#0
	sta	outbuflen
	sta	oindex
	;;
	;;
	;; 
	lda	#1
	sta	cioerr
	lda	#0
	sta	cioret
	jsr	get_cio_return_values
	rts

	
cio_put:
	jsr	wifi_hot
	cmp	#FUJI_OK
	beq	cio_put_continue
	lda	#136
	sta	cioerr
	sta	cioret
	jsr	get_cio_return_values
	rts
cio_put_continue:
	lda	DVSTAT2
	cmp	#0
	bne	cio_put_2
	lda	#136
	sta	cioerr
	lda	#0
	sta	cioret
	jsr	get_cio_return_values
	rts
cio_put_2:			; at this point the char to process is in accum
	pha			; save character
	pha			; twice...
	ldx	#VARIABLE_OUTBUF
	jsr	getvar
	ldx	oindex		; load current outbuf index
	inx			; increment by one
	pla			; get character to write
	sta 	tempw,x		; store character
	txa
	sta	oindex		; store new output index
	pla			; restore second copy
	cmp	#$9b		; enter? (LF)
	beq	cio_put_send	; ship it!
	lda	oindex		; are we at the end of our buffer?
	cmp	#$ff
	beq	cio_put_send	; def. ship it now.
	jmp	cio_put_exit
cio_put_send:
	jsr	cio_put_flush	; if buffer is full or EOL or ENTER then flush the buffer
cio_put_exit:			; throw success back to the OS
	lda	#0
	sta	cioret
	lda	#1
	sta	cioerr
	jsr	get_cio_return_values
	rts


;;;
;;; (good) cio_status
;;; 
cio_status:
	jsr	wifi_hot
	cmp	#FUJI_OK
	beq	cio_status_continue
	lda	#146
	sta	cioerr
	lda	#146
	sta	cioret
	jsr	get_cio_return_values
	rts	
cio_status_continue:
	lda	#NDEV
	sta	DDEVIC
	lda	ZUNIT
	sta	DUNIT
	lda	#'s'		; 's' command - get_tcp_status
	sta     DCOMND
	lda	#DSTATS_READ
	sta	DSTATS
	lda	#<DVSTAT0       ; we're getting 4 bytes back
	sta	DBUFLO
	lda	#>DVSTAT0
	sta	DBUFHI
	lda	#4		; 4 bytes dvstat1-4
	sta	DBYTLO
	lda	#0
	sta	DBYTHI
	lda	#DTIMLO_DEFAULT	; 15 seconds
	sta	DTIMLO
	lda	#0		; AUX1 = 0 is "get muh status"
	sta	DAUX1
	lda	#0
	sta	DAUX2
	jsr	SIOV
	lda	DVSTAT0
	sta	cioret
	lda	#1
	stx	cioerr
	jsr	get_cio_return_values
	rts

	;; dcmd:	.byte 0 dstats:	.byte 0 dbyt:	.byte 0

	
cio_special:
	lda	status
	cmp	FUJI_OK
	beq	cio_special_continue
	lda	#146
	sta	cioerr
	lda	#146
	sta	cioret
	jsr	get_cio_return_values
	rts
cio_special_continue:	
	lda	ZCOMND
	cmp	#SPECIAL_FLUSH
	bne	cio_special_next_command_1
	jsr	cio_put_flush
	lda	#1
	ldy	#1
	rts
cio_special_next_command_1:
	lda	#1
	ldy	#1
	rts

	
cio_vector:
	ldy	#1		; successful IO function
	rts


;;;
;;; called by various routines to check fujinet status - is
;;; the wifi connection active e.g. blue light on?
;;;
;;; sets accum and STATUS to FUJI_DEAD/FUJI_OK.
;;; 
wifi_hot:
	lda	#FUJI_DEAD
	sta	status
	lda	#NDEV
	sta	DDEVIC
	lda	ZUNIT
	sta	DUNIT
	lda	#$fa		; is fuji wifi hot?
	sta     DCOMND
	lda	#DSTATS_READ
	sta	DSTATS
	;;
	;; get address of status byte buffer into tempw
	;; 
	ldx	#VARIABLE_STATUS
	jsr	getvar
	;;
	;; tempw points to global variable BUFFER
	;; 
	lda	tempw		; we're looking for a single status byte back
	sta	DBUFLO
	lda	tempw+1
	sta	DBUFHI
	lda	#1		; 1 status byte
	sta	DBYTLO
	lda	#0
	sta	DBYTHI
	lda	#DTIMLO_DEFAULT
	sta	DTIMLO
	lda	ZAUX1
	sta	DAUX1
	lda	ZAUX2
	sta	DAUX2
	jsr	SIOV
	lda	status
	cmp	#FUJI_OK
	beq	wifi_hot_ok
	lda	#FUJI_DEAD
	sta	status
wifi_hot_ok:	
	lda	status
	rts


	
CIOV:	ldx	#$00
	jsr	$e456
	rts

	
SIOV:	jsr	$e459
	jsr	set_cio_return_values
	rts

	
;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;
;;; 
;;; one time init code for the module - set up
;;; cio / hatabs stuff.
;;;
;;; is called after relocation occurs
;;; 
;;; gets cleared from memory post remap so length
;;; doesn't matter here.
;;;
init:
	;; 
	;; assume failure with init
	;; 
	lda	#FUJI_DEAD
	sta	status
	;; 
	;; 	init buffers/vars
	;;
	lda	#0
	sta	iindex
	sta	oindex
	sta	inbuflen
	sta	outbuflen
	sta	cioret
	sta	cioerr
	;; 
	;; copy cio routines to ntab, could probably just use
	;; functab to save 14 or so bytes.
	;; 
	ldx	#0
init_copy:
	lda	functab+3,x
	sta	ntab,x
	inx
	lda	functab+3,x
	sta	ntab,x
	inx
	cpx	#14
	bne	init_copy
	;; 
	;; search for open hatabs entry - might be full but
	;; probably not.  Should probably do this before
	;; above copy but it's a rare thing to have a full
	;; HATAB table.
	;; 
	ldy	#0		; first HATABS entry
htabs_loop:
	lda	HATABS,y	 ; get device name
	cmp	#0		 ; is it 0?
	beq	found_empty_slot ; yes, we have a free HATABS slot
	iny			 ; skip over device name
	iny			 ; skip over nandler...
	iny			 ; ...table pointer
	cpy	#34		 ; end of the line?
	bne	htabs_loop	 ; nope - check next slot
	ldx	#$00		 ; tell cc65 that HATABS was...
	lda	FUJI_FULL_TAB	 ; ...full with error code 2
	sta	status
	rts			 ; bail as HATABS full.
	;; 
	;; we have an empty HATABS slot so fill it in with our
	;; device name ('N') and a pointer to the table
	;; 
found_empty_slot:		 ; add FujiNET device to HATABS
	lda	#'N'		 ; set N: driver
	sta	HATABS,y
	;; 
	;; get pointer to the relocated ntab - it's weird we have
	;; to do it this way - but after trying 4 or so different
	;; versions settled on this.  It's basically dealing with
	;; a level of indirection here.
	;; 
	ldx	#VARIABLE_NTAB
	jsr	getvar
	;; 
	;; stuff it into HATAB
	;; 
	iny
	lda	tempw		 ; set HATABS table address
	sta	HATABS,y
	iny
	lda	tempw+1
	sta	HATABS,y
	;;
	;; now call cio_status to see if the device is up
	;; and running (blue dot on?)
	;;
	jsr	wifi_hot	 ; check wifi and return in Accum so cc65 can pick it up
exit_init:	
	rts

_reloc_end:

	.end

