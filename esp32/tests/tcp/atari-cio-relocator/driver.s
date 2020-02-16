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
	
	DVSTAT0	= $02ea		; device error & command status byte
	DVSTAT1 = $02eb		; device status byte
	DVSTAT2 = $02ec		; maximum device timeout in seconds
	DVSTAT3 = $02ed		; number of bytes in output buffer
	
	;; from sio.h
	NDEV           = $70	; atariwifi device number
	DSTATS_NONE    = $00
	DSTATS_READ    = $40
	DSTATS_WRITE   = $80
	DTIMLO_DEFAULT = $0f
	DBYT_NONE      = $00
	DBYT_OPEN      = $ff

	;; FujiNET return values
	FUJI_DEAD      = $00	; FN not responding
	FUJI_OK	       = $01	; FN good to go
	FUJI_FULL_TAB  = $02	; HATABS is full (unlikely)
	
	.code
	
_reloc_begin:

functab:
	.byte 9			; (0) exposing nine addresses - these are remapped
	.addr init		; (1) driver init function
	.addr cio_open - 1	; (3) cio stuff
	.addr cio_close - 1	; (5)
	.addr cio_get - 1	; (7)
	.addr cio_put - 1	; (9)
	.addr cio_status - 1	; (11)
	.addr cio_special - 1	; (13)
	.addr cio_vector	; (15)
	.addr ntab
	
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
iindex:	.byte 0			; what character are we resting on in the input buffer?
inbuflen:	.byte 0
outbuf:	.res $ff, $00		; output buffer
oindex:	.byte 0			; what character are we resting on in the output buffer?
outbuflen:	.byte 0

_reloc_code_begin:	

	
aux_save:
	ldy	ZUNIT
	lda	ZAUX1
	sta	aux1sv,y
	lda	ZAUX2
	sta	aux2sv,y
	rts
	
;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;
;;;
;;; default_cio_values
;;;
;;; load up a CIO command frame with defaults
;;; 
default_cio_values:
	lda	NDEV
	sta	DDEVIC
	lda	DTIMLO_DEFAULT
	sta	DTIMLO
	lda	#$00
	sta	DTIMHI
	lda	aux1
	sta	DAUX1
	lda	aux2
	sta	DAUX2
	lda	DTIMLO_DEFAULT
	sta	DTIMLO
	lda	#$00
	sta	DTIMHI
	rts

	
get_cio_return_values:
	lda	cioret
	ldy	cioerr
	rts

	
set_cio_return_values:
	sta	cioret
	sty	cioerr
	rts

	
cio_open:
	lda	status
	cmp	FUJI_OK
	beq	cio_open_continue
	lda	#146
	sta	cioerr
	lda	#0
	sta	cioret
	jsr	get_cio_return_values
	rts
cio_open_continue:
	jsr	aux_save
	lda	NDEV
	sta	ZDEVIC
	lda	#'o'
	sta     DCOMND
	lda	DSTATS_WRITE
	sta	DSTATS
	lda	ZBUFLO
	sta	DBUFLO
	lda	ZBUFHI
	sta	DBUFHI
	lda	DBYT_OPEN
	sta	DBYTLO
	lda	DTIMLO_DEFAULT
	sta	DTIMLO
	ldy	ZUNIT
	lda	aux1sv,y
	sta	DAUX1
	lda	aux2sv,y
	sta	DAUX2	
	jsr	SIOV
	;; 
	;; clear input and output buffer length
	;; 
	lda	#0
	sta	inbuflen
	sta	outbuflen
	sta	iindex
	sta	oindex
	
	lda	#1		; cio return
	ldy	#1		; cio error
	rts

	
cio_close:
	lda	status
	cmp	FUJI_OK
	beq	cio_close_continue
	lda	#146
	sta	cioerr
	lda	#0
	sta	cioret
	jsr	get_cio_return_values
	rts
cio_close_continue:	
	jsr	cio_put_flush
	lda	NDEV
	sta	ZDEVIC
	lda	#'o'
	sta     DCOMND
	lda	DSTATS_NONE
	sta	DSTATS
	lda	#$00
	sta	DBUFLO
	sta	DBUFHI
	lda	DBYT_NONE
	sta	DBYTLO
	lda	DTIMLO_DEFAULT
	sta	DTIMLO
	ldy	ZUNIT
	lda	aux1sv,y
	sta	DAUX1
	lda	aux2sv,y
	sta	DAUX2
	jsr 	SIOV	
	ldy	ZUNIT
	lda	#$00
	sta	aux1sv,y
	sta	aux2sv,y
	lda	#$01		; cio return
	ldy	#$01		; cio err
	rts

	
cio_get:
	lda	status
	cmp	FUJI_OK
	beq	cio_get_continue
	lda	#146
	sta	cioerr
	lda	#0
	sta	cioret
	jsr	get_cio_return_values
	rts
cio_get_continue:
	brk
	lda	#0		; used to store first character read
	sta	tempb

	lda	#1
	sta	cioerr
	lda	inbuflen
	cmp	#0
	beq	cio_get_1
	jmp	cio_get_2
cio_get_1:
	jsr	cio_status
	lda	DVSTAT0
	sta	inbuflen
	lda	#0
	sta	iindex
	lda	NDEV
	sta	ZDEVIC
	lda	#'r'
	sta     DCOMND
	lda	DSTATS_READ
	sta	DSTATS
	lda	inbuf
	sta	DBUFLO
	lda	inbuf+1
	sta	DBUFHI
	lda	inbuflen
	sta	DBYTLO
	lda	DTIMLO_DEFAULT
	sta	DTIMLO
	lda	inbuflen
	sta	DAUX1
	lda	#$00
	sta	DAUX2
	jsr	SIOV
	ldy	#0
	lda	inbuf,y
	sta	tempb		; first character read
cio_get_2:			; do we have data?
	lda	inbuflen
	cmp	#0
	beq	cio_get_3
	jmp	cio_get_5
cio_get_3:			; is dvstat2 == 0
	lda	DVSTAT2
	cmp	#0
	beq	cio_get_4
	jmp	cio_get_5
cio_get_4:			; no data and dvstat2 == 0
	lda	#136
	sta	cioerr
	sta	cioret
	jsr	get_cio_return_values
	rts
cio_get_5:			; check ZIOCB command
	lda	ZCOMND
	and	#$02
	cmp	#0
	beq	cio_get_6

	lda	tempb
	cmp	#$0a
	bne	cio_get_6
	lda	#$9b
	sta	tempb
cio_get_6:
	lda	tempb
	cmp	#$0d
	bne	cio_get_7
	lda	#$20
	sta	tempb
cio_get_7:	
	lda	inbuflen
	cmp	iindex
	beq	cio_get_8
	lda	tempb
	sta	cioret
	inc	iindex
cio_get_8:	
	jsr	get_cio_return_values
	rts

	
cio_put_flush:
	lda	NDEV
	sta	ZDEVIC
	lda	#'w'
	sta     DCOMND
	lda	DSTATS_WRITE
	sta	DSTATS
	lda	outbuf
	sta	DBUFLO
	lda	outbuf+1
	sta	DBUFHI
	lda	outbuflen
	sta	DBYTLO
	lda	DTIMLO_DEFAULT
	sta	DTIMLO
	lda	outbuflen
	sta	DAUX1
	lda	#$00
	sta	DAUX2
	jsr	SIOV
	lda	#$00
	sta	outbuflen
	sta	oindex	
	jsr	get_cio_return_values
	rts

	
cio_put:
	lda	status
	cmp	FUJI_OK
	beq	cio_put_continue
	lda	#146
	sta	cioerr
	lda	#0
	sta	cioret
	jsr	get_cio_return_values
	rts
cio_put_continue:
	brk
	lda	DVSTAT2
	cmp	#0
	bne	cio_put_2
	lda	#136
	sta	cioerr
	lda	#0
	sta	cioret
	jsr	set_cio_return_values
	rts
cio_put_2:
	lda	cioret
	cmp	#$9b		; end of line?
	beq	cio_put_3
	lda	#$fe
	cmp	outbuflen
	bcs	cio_put_3	;
	jmp	cio_put_4
cio_put_3:
	ldx	outbuflen
	lda	#$0d
	sta	outbuf,x
	inx
	lda	#$0a
	sta	outbuf,x
	txa
	sta	outbuflen
	jsr	cio_put_flush
	jmp	cio_put_5
cio_put_4:
	ldx	outbuflen
	lda	cioret
	sta	outbuf,x
	txa
	sta	outbuflen
cio_put_5:
	jsr	set_cio_return_values
	rts
	
cio_status:
	lda	NDEV
	sta	ZDEVIC
	lda	#'s'
	sta     DCOMND
	lda	DSTATS_READ
	sta	DSTATS
	lda	DVSTAT0		; load dvstat bytes (4)
	sta	DBUFLO
	lda	DVSTAT0+1
	sta	DBUFHI
	lda	#4		; 4 dvstat bytes
	sta	DBYTLO
	lda	DTIMLO_DEFAULT
	sta	DTIMLO
	lda	ZAUX1
	sta	DAUX1
	lda	ZAUX2
	sta	DAUX2
	jsr	SIOV
	lda	#$00
	sta	outbuflen
	lda	DVSTAT0		; return dvstat0 in A
	ldy	#$01		; good CIO call
	rts
	
dcmd:	.byte 0
dstats:	.byte 0
dbyt:	.byte 0

	
cio_special:
	lda	status
	cmp	FUJI_OK
	beq	cio_special_continue
	lda	#146
	sta	cioerr
	lda	#0
	sta	cioret
	jsr	get_cio_return_values
	rts
cio_special_continue:
	brk
	lda	#$01
	sta	cioerr
	lda	#$00
	sta	dcmd
	sta	dstats
	lda	ZCOMND
	cmp	#16
	bne	cio_special_next_1
	lda	#'a'
	sta	dcmd
	jmp	cio_special_make_call
cio_special_next_1:
	lda	ZCOMND
	cmp	#17
	bne	cio_special_next_2
	lda	#'u'
	sta	dcmd
	jmp	cio_special_make_call
cio_special_next_2:
	lda	#146
	sta	cioerr
cio_special_make_call:	
	jsr	default_cio_values
	lda	dcmd
	sta	DCOMND
	lda	ZUNIT
	sta	DUNIT
	lda	dstats
	sta	DSTATS
	jsr	SIOV
	rts

	
cio_vector:
	ldy	#1		; successful IO function
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
	;; assume failure with init
	lda	FUJI_DEAD
	sta	status
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
	ldx	#17
	lda	functab,x
	sta	tempw
	lda	functab+1,x
	sta	tempw+1
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
	;; and running
	;; 
	jsr	cio_status	 ; call CIO to see if we're live
	cpy	#$01
	beq	failed_init	 ; nope...we aren't.
 	ldx	#0
	lda	FUJI_OK		 ; we're good to go! return to cc65.
	sta	status
	rts
failed_init:
	ldx	#0
	lda	FUJI_DEAD	 ; FujiNET unresponsive
	sta	status
exit_init:	
	rts
	
_reloc_end:

	.end

