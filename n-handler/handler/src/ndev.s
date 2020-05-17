	;; N: Device Handler
	;; Compile with ATASM

	;; Author: Thomas Cherryhomes
	;;   <thom.cherryhomes@gmail.com>

	;; CURRENT IOCB IN ZERO PAGE
	
ZIOCB   =     $20      ; ZP IOCB
ZICHID  =     ZIOCB    ; ID
ZICDNO  =     ZIOCB+1  ; UNIT #
ZICCOM  =     ZIOCB+2  ; COMMAND
ZICSTA  =     ZIOCB+3  ; STATUS
ZICBAL  =     ZIOCB+4  ; BUF ADR LOW
ZICBAH  =     ZIOCB+5  ; BUF ADR HIGH
ZICPTL  =     ZIOCB+6  ; PUT ADDR L
ZICPTH  =     ZIOCB+7  ; PUT ADDR H
ZICBLL  =     ZIOCB+8  ; BUF LEN LOW
ZICBLH  =     ZIOCB+9  ; BUF LEN HIGH
ZICAX1  =     ZIOCB+10 ; AUX 1
ZICAX2  =     ZIOCB+11 ; AUX 2
ZICAX3  =     ZIOCB+12 ; AUX 3
ZICAX4  =     ZIOCB+13 ; AUX 4
ZICAX5  =     ZIOCB+14 ; AUX 5
ZICAX6  =     ZIOCB+15 ; AUX 6

DOSINI  =     $0C      ; DOSINI

       ; INTERRUPT VECTORS
       ; AND OTHER PAGE 2 VARS

VPRCED  =     $0202   ; PROCEED VCTR
COLOR2  =     $02C6   ; MODEF BKG C
MEMLO   =     $02E7   ; MEM LO
DVSTAT  =     $02EA   ; 4 BYTE STATS

       ; PAGE 3
       ; DEVICE CONTROL BLOCK (DCB)

DCB     =     $0300   ; BASE
DDEVIC  =     DCB     ; DEVICE #
DUNIT   =     DCB+1   ; UNIT #
DCOMND  =     DCB+2   ; COMMAND
DSTATS  =     DCB+3   ; STATUS/DIR
DBUFL   =     DCB+4   ; BUF ADR L
DBUFH   =     DCB+5   ; BUF ADR H
DTIMLO  =     DCB+6   ; TIMEOUT (S)
DRSVD   =     DCB+7   ; NOT USED
DBYTL   =     DCB+8   ; BUF LEN L
DBYTH   =     DCB+9   ; BUF LEN H
DAUXL   =     DCB+10  ; AUX BYTE L
DAUXH   =     DCB+11  ; AUX BYTE H

HATABS  =     $031A   ; HANDLER TBL

       ; IOCB'S * 8

IOCB    =     $0340   ; IOCB BASE
ICHID   =     IOCB    ; ID
ICDNO   =     IOCB+1  ; UNIT #
ICCOM   =     IOCB+2  ; COMMAND
ICSTA   =     IOCB+3  ; STATUS
ICBAL   =     IOCB+4  ; BUF ADR LOW
ICBAH   =     IOCB+5  ; BUF ADR HIGH
ICPTL   =     IOCB+6  ; PUT ADDR L
ICPTH   =     IOCB+7  ; PUT ADDR H
ICBLL   =     IOCB+8  ; BUF LEN LOW
ICBLH   =     IOCB+9  ; BUF LEN HIGH
ICAX1   =     IOCB+10 ; AUX 1
ICAX2   =     IOCB+11 ; AUX 2
ICAX3   =     IOCB+12 ; AUX 3
ICAX4   =     IOCB+13 ; AUX 4
ICAX5   =     IOCB+14 ; AUX 5
ICAX6   =     IOCB+15 ; AUX 6

       ; HARDWARE REGISTERS

PACTL   =     $D302   ; PIA CTRL A

       ; OS ROM VECTORS

CIOV    =     $E456   ; CIO ENTRY
SIOV    =     $E459   ; SIO ENTRY

       ; CONSTANTS

PUTREC  =     $09     ; CIO PUTREC
DEVIDN  =     $71     ; SIO DEVID
DSREAD  =     $40     ; FUJI->ATARI
DSWRIT  =     $80     ; ATARI->FUJI
MAXDEV  =     4       ; # OF N: DEVS
EOF     =     $88     ; ERROR 136
EOL     =     $9B     ; EOL CHAR

;;; Macros ;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;

	.MACRO DCBC
	.LOCAL
	LDY	#$0C
?DCBL	LDA	%%1,Y
	STA	DCB,Y
	DEY
	BPL	?DCBL
	.ENDL
	.ENDM
		
;;; Initialization ;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;

	org $3100
	
START:	
	LDA	DOSINI
	STA	DSAV
	STA	RESET+1
	LDA	DOSINI+1
	STA	DSAV+1
	STA	RESET+2
	LDA	#<RESET
	STA	DOSINI
	LDA	#>RESET
	STA	DOSINI+1

	;;  Alter MEMLO
	
	LDA	#<PGEND		
	STA	MEMLO
	LDA	#>PGEND
	STA	MEMLO+1

	BVC	IHTBS
	
RESET:
	JSR	$FFFF		; Jump to extant DOSINI
	JSR	IHTBS		; Insert into HATABS

	;;  Alter MEMLO
	
	LDA	#<PGEND		
	STA	MEMLO
	LDA	#>PGEND
	STA	MEMLO+1

	;; Back to DOS
	
	RTS

	;; Insert entry into HATABS
	
IHTBS:
	LDY	#$00
IH1	LDA	HATABS,Y
	BEQ	HFND
	CMP	#'N'
	BEQ	HFND
	INY
	INY
	INY
	CPY	#11*3
	BCC	IH1

	;; Found a slot

HFND:
	LDA	#'N'
	STA	HATABS,Y
	LDA	#<CIOHND
	STA	HATABS+1,Y
	LDA	#>CIOHND
	STA	HATABS+2,Y

	;; And we're done with HATABS

	;; Query FUJINET

	JSR	STPOLL

	;; Output Ready/Error

OBANR:
	LDX	#$00		; IOCB #0
	LDA	#PUTREC
	STA	ICCOM,X
	LDA	#$28		; 40 CHARS Max
	STA	ICBLL,X
	LDA	#$00
	STA	ICBLH,X
	LDA	DSTATS		; Check DSTATS
	BPL	OBRDY		; < 128 = Ready

	;; Status returned error.
	
OBERR:
	LDA	#<BERROR
	STA	ICBAL,X
	LDA	#>BERROR
	STA	ICBAH,X
	BVC	OBCIO

	;; Status returned ready.
	
OBRDY:	
	LDA	#<BREADY
	STA	ICBAL,X
	LDA	#>BREADY
	STA	ICBAH,X

OBCIO:
	JSR	CIOV

	;; Vector in proceed interrupt

SPRCED:
	LDA	#<PRCVEC
	STA	VPRCED
	LDA	#>PRCVEC
	STA	VPRCED+1

	;; And we are done, back to DOS.
	
	RTS

;;; End Initialization Code ;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;

;;; CIO OPEN ;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;

OPEN:
	;; Prepare DCB
	
	JSR	GDIDX		; Get Device ID in X (0-3)
	LDA	ZICDNO		; IOCB UNIT # (1-4)
	STA	OPNDCB+1	; Store in DUNIT
	LDA	ZICBAL		; Get filename buffer
	STA	OPNDCB+4	; stuff in DBUF
	LDA	ZICBAH		; ...
	STA	OPNDCB+5	; ...
	LDA	ZICAX1		; Get desired AUX1/AUX2
	STA	OPNDCB+10	; Save them, and store in DAUX1/DAUX2
	STA	AX1SV,X		; ...
	LDA	ZICAX2		; ...
	STA	OPNDCB+11	; ...
	STA	AX2SV,X		; ...

	;;  Copy DCB template to DCB
	
	DCBC	OPNDCB

	;;  Send to #FujiNet
	
	JSR	SIOV
                                    
	;; Return DSTATS, unless 144, then get extended error
	
OPCERR:
	LDY	DSTATS		; GET SIO STATUS
	CPY	#$90		; ERR 144?
	BNE	OPDONE		; NOPE. RETURN DSTATS
       
	;; 144 - get extended error

	JSR	STPOLL  	; POLL FOR STATUS
	LDY	DVSTAT+3

       ; RESET BUFFER LENGTH + OFFSET
       
OPDONE:
	LDA	#$01
	STA	TRIP
	JSR     GDIDX
	LDA     #$00
	STA     RLEN,X
	STA     TOFF,X
	STA     ROFF,X
	TYA
	RTS             ; AY = ERROR

OPNDCB:
	.BYTE      DEVIDN  ; DDEVIC
	.BYTE      $FF     ; DUNIT
	.BYTE      'O'     ; DCOMND
	.BYTE      $80     ; DSTATS
	.BYTE      $FF     ; DBUFL
	.BYTE      $FF     ; DBUFH
	.BYTE      $0F     ; DTIMLO
	.BYTE      $00     ; DRESVD
	.BYTE      $00     ; DBYTL
	.BYTE      $01     ; DBYTH
	.BYTE      $FF     ; DAUX1
	.BYTE      $FF     ; DAUX2
	
;;; End CIO OPEN ;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;

;;; CIO CLOSE ;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;

CLOSE:	
	JSR     DIPRCD		; Disable Interrupts
	LDA     ZICDNO		; IOCB Unit #
	STA     CLODCB+1	; to DCB...
	
	DCBC	CLODCB		; Copy DCB into place

	JSR	SIOV

	JSR	PFLUSH		; Do a Put Flush if needed.

	LDY	DSTATS		; Return SIO status
	TYA
	RTS			; Done.

CLODCB .BYTE	DEVIDN		; DDEVIC
       .BYTE	$FF		; DUNIT
       .BYTE	'C'		; DCOMND
       .BYTE	$00		; DSTATS
       .BYTE	$00		; DBUFL
       .BYTE	$00		; DBUFH
       .BYTE	$0F		; DTIMLO
       .BYTE	$00		; DRESVD
       .BYTE	$00		; DBYTL
       .BYTE	$00		; DBYTH
       .BYTE	$00		; DAUX1
       .BYTE	$00		; DAUX2
	
;;; End CIO CLOSE ;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;

;;; CIO GET ;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;

GET:
	JSR	GDIDX		; IOCB UNIT #-1 into X 
	LDA	RLEN,X		; Get # of RX chars waiting
	BNE     GETDISC		; LEN > 0?

	;; If RX buffer is empty, get # of chars waiting...
	
	JSR	STPOLL		; Status Poll
	JSR	GDIDX		; IOCB UNIT -1 into X (because Poll trashes X)
	LDA	DVSTAT		; # of bytes waiting (0-255)
	STA	RLEN,X		; Store in RX Len
	BNE     GETDO		; We have something waiting...

	;; At this point, if RLEN is still zero, then return
	;; with an EOF.
	
	LDY     #EOF		; ERROR 136 - End of File
	LDA     #EOF
	RTS

GETDO:
	LDA	ZICDNO		; Get IOCB UNIT #
	STA	GETDCB+1	; Store into DUNIT
	JSR	ICD2B		; A = IOCB's buffer high page
	STA	GETDCB+5	; store into DBUFH
	LDA	DVSTAT		; # of bytes waiting
	STA	GETDCB+8	; Store into DBYT...
	STA	GETDCB+10	; and DAUX1...
       
	DCBC	GETDCB		; Prepare DCB
	
	JSR	SIOV		; Call SIO to do the GET

	;; Clear the Receive buffer offset.
	
	JSR	GDIDX		; IOCB UNIT #-1 into X
	LDA	#$00
	STA     ROFF,X

GETDISC:
	LDA     DVSTAT+2	; Did we disconnect?
	BNE     GETUPDP		; nope, update the buffer cursor.

	;; We disconnected, emit an EOF.
	
	LDA	#EOF
	LDY	#EOF
	RTS			; buh-bye.

GETUPDP:
	DEC     RLEN,X		; Decrement RX length.
	LDY     ROFF,X		; Get RX offset cursor.

	;; Return Next char from appropriate RX buffer.
	
G3:	CPX	#$03		; Buffer for N4:
	BNE	G2
	LDA	RBUF+$300,y
	BVC	GX
	
G2:	CPX	#$02		; Buffer for N3:
	BNE	G1
	LDA	RBUF+$200,y
	BVC	GX
	
G1:	CPX	#$01		; Buffer for N2:
	BNE	G0
	LDA	RBUF+$100,y
	BVC	GX
	
G0:	LDA	RBUF,y		; Buffer for N1:
	BVC	GX

	;; Increment RX offset
	
GX:	INC	ROFF,X		; Increment RX offset.
	TAY			; stuff returned val into Y temporarily.

	;; If requested RX buffer is empty, reset TRIP.

	LDA	RLEN,X
	BNE	GETDONE
	LDA     #$00
	STA     TRIP

	;; Return byte back to CIO.
	
GETDONE:
	TYA			; Move returned val back.
	LDY	#$01		; SUCCESS

	RTS			; DONE...

GETDCB .BYTE     DEVIDN  ; DDEVIC
       .BYTE     $FF     ; DUNIT
       .BYTE     'R'     ; DCOMND
       .BYTE     $40     ; DSTATS
       .BYTE     $00     ; DBUFL
       .BYTE     $FF     ; DBUFH
       .BYTE     $0F     ; DTIMLO
       .BYTE     $00     ; DRESVD
       .BYTE     $FF     ; DBYTL
       .BYTE     $00     ; DBYTH
       .BYTE     $FF     ; DAUX1
       .BYTE     $00     ; DAUX2
	
;;; End CIO GET ;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;

;;; CIO PUT ;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;

PUT:
	;; Add to TX buffer.
       
	JSR	GDIDX
	LDY	TOFF,X  ; GET TX cursor.
	
P3:	CPX	#$03		; N4: TX buffer
	BNE	P2
	STA	TBUF+$300,Y
	BVC	POFF
	
P2:	CPX	#$02		; N3: TX buffer
	BNE	P1
	STA	TBUF+$200,Y
	BVC	POFF
	
P1:	CPX	#$01		; N2: TX buffer
	BNE	P0
	STA	TBUF+$100,Y
	BVC	POFF
	
P0:	STA	TBUF,Y		; N1: TX buffer
POFF:	INC	TOFF,X		; Increment TX cursor
	LDY	#$01		; SUCCESSFUL

	;; Do a PUT FLUSH if EOL or buffer full.

	CMP     #EOL    ; EOL?
	BEQ     FLUSH  ; FLUSH BUFFER
	JSR     GDIDX   ; GET OFFSET
	LDY     TOFF,X
        CPX     #$FF    ; LEN = $FF?
        BEQ     FLUSH  ; FLUSH BUFFER
        RTS

       ; FLUSH BUFFER, IF ASKED.

FLUSH  JSR     PFLUSH  ; FLUSH BUFFER
       RTS

PFLUSH:	

       ; CHECK CONNECTION, AND EOF
       ; IF DISCONNECTED.

       JSR     STPOLL  ; GET STATUS
       LDA     DVSTAT+2
       BNE     PF1   
       LDY     #EOF
       LDA     #EOF
       RTS

PF1:	JSR     GDIDX   ; GET DEV X
       LDA     TOFF,X
       BNE     PF2
       JMP     PDONE

       ; FILL OUT DCB FOR PUT FLUSH

PF2:	LDA     ZICDNO
       STA     PUTDCB+1

       ; PICK APROPOS BUFFER PAGE
       
TB3:	CPX     #$03		; N4: TX Buffer
	BNE     TB2
	LDA	#>TBUF+3
	BVC	TBX

TB2:	CPX     #$02		; N3: TX Buffer
	BNE     TB1
	LDA	#>TBUF+2
	BVC	TBX

TB1:	CPX     #$01		; N4: TX Buffer
	BNE     TB0
	LDA	#>TBUF+1
	BVC	TBX

TB0:	LDA	#>TBUF

       ; FINISH DCB AND DO SIOV

TBX:	STA     PUTDCB+5
	LDA     TOFF,X
	STA     PUTDCB+8
	STA     PUTDCB+10

	DCBC	PUTDCB
	JSR     SIOV
       
       ; CLEAR THE OFFSET CURSOR
       ; AND LENGTH

       JSR     GDIDX
       LDA     #$00
       STA     TOFF,X

PDONE:	LDY     #$01
       RTS

PUTDCB .BYTE      DEVIDN  ; DDEVIC
       .BYTE      $FF     ; DUNIT
       .BYTE      'W'     ; DCOMND
       .BYTE      $80     ; DSTATS
       .BYTE      $00     ; DBUFL
       .BYTE      $FF     ; DBUFH
       .BYTE      $0F     ; DTIMLO
       .BYTE      $00     ; DRESVD
       .BYTE      $FF     ; DBYTL
       .BYTE      $00     ; DBYTH
       .BYTE      $FF     ; DAUX1
       .BYTE      $00     ; DAUX2
	
;;; End CIO PUT ;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;
	
;;; CIO STATUS ;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;

STATUS:
	JSR     ENPRCD  ; ENABLE PRCD
       JSR     GDIDX   ; GET DEVICE#
       LDA     RLEN,X  ; GET RLEN
       BNE     STSLEN  ; RLEN > 0?
       LDA     TRIP
       BNE     STTRI1  ; TRIP = 1?

       ; NO TRIP, RETURN SAVED LEN

STSLEN LDA     RLEN,X  ; GET RLEN
       STA     DVSTAT  ; RET IN DVSTAT
       LDA     #$00
       STA     DVSTAT+1
       JMP     STDONE  ; DONE.

       ; DO POLL AND UPDATE RCV LEN

STTRI1 JSR     STPOLL  ; POLL FOR ST
       
       ; IS <= 256?

       LDA     DVSTAT+1
       BNE     STTRI2  ; > 256
       STA     RLEN,X
       JMP     STTRIU  ; UPDATE TRIP

       ; > 256, SET TO 256

STTRI2 LDA     #$FF
       STA     RLEN,X

       ; UPDATE TRIP FLAG

STTRIU BNE     STDONE
       STA     TRIP    ; RLEN = 0

       ; RETURN CONNECTED? FLAG.

STDONE LDA     DVSTAT+2
       RTS

       ; ASK FUJINET FOR STATUS

STPOLL:	
       LDA     ZICDNO  ; IOCB #
       STA     STADCB+1

	DCBC	STADCB
	
       JSR     SIOV    ; DO IT...

       ; MAX 256 BYTES WAITING.

       LDA     DVSTAT+1
       BEQ     STP2
       LDA     #$FF
       STA     DVSTAT
       LDA     #$00
       STA     DVSTAT+1

       ; A = CONNECTION STATUS

STP2   LDA     DVSTAT+2
       RTS

STADCB .BYTE      DEVIDN  ; DDEVIC
       .BYTE      $FF     ; DUNIT
       .BYTE      'S'     ; DCOMND
       .BYTE      $40     ; DSTATS
       .BYTE      $EA     ; DBUFL
       .BYTE      $02     ; DBUFH
       .BYTE      $0F     ; DTIMLO
       .BYTE      $00     ; DRESVD
       .BYTE      $04     ; DBYTL
       .BYTE      $00     ; DBYTH
       .BYTE      $00     ; DAUX1
       .BYTE      $00     ; DAUX2
	
;;; End CIO STATUS ;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;

;;; CIO SPECIAL ;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;

SPEC:
       ; HANDLE LOCAL COMMANDS.

       LDA     ZICCOM
       CMP     #$0F    ; 15 = FLUSH
       BNE     S1      ; NO.
       JSR     PFLUSH  ; DO FLUSH
       LDY     #$01    ; SUCCESS
       RTS

       ; HANDLE SIO COMMANDS.
       ; GET DSTATS FOR COMMAND

S1:	LDA     #DEVIDN ; $71
       STA     DDEVIC
       LDA     ZICDNO  ; UNIT #
       STA     DUNIT
       LDA     #$FF    ; DS INQ
       STA     DCOMND
       LDA     #DSREAD
       STA     DSTATS
       LDA     #<INQDS
       STA     DBUFL
       LDA     #>INQDS
       STA     DBUFH
       LDA     #$01
       STA     DBYTL
       LDA     #$00
       STA     DBYTH
       STA     DAUXH
       LDA     #$0F
       STA     DTIMLO
       LDA     ZICCOM
       STA     DAUXL
       JSR     SIOV    ; DO IT...

       LDA     DSTATS
       BPL     :DSOK
DSERR:
	TAY             ; RET THE ERR
       RTS

       ; WE GOT A DSTATS INQUIRY
       ; IF $FF, THE COMMAND IS
       ; INVALID

DSOK:
	LDA     INQDS
       CMP     #$FF    ; INVALID?
       BNE     DSGO   ; DO THE CMD
       LDY     #$92    ; UNIMP CMD
       TYA
       RTS

	;; Do the special, since we want to pass in all the IOCB
	;; Parameters to the DCB, This is being done long-hand.
	
DSGO:	LDA	#DEVIDN
	STA	DDEVIC
	LDA	ZICDNO
	STA	DUNIT
	LDA	ZICCOM
	STA	DCOMND
	LDA	INQDS
	STA	DSTATS
	LDA	ZICBAL
	STA	DBUFL
	LDA	ZICBAH
	STA	DBUFH
	LDA	#$0F
	STA	DTIMLO
	LDA	#$00		; 256 bytes
	STA	DBYTL
	LDA	#$01
	STA	DBYTH
	LDA	ZICAX1
	STA	DAUXL
	LDA	ZICAX2
	STA	DAUXH

	JSR	SIOV

	;; Return DSTATS in Y and A

	LDA	DSTATS
	TAY

	RTS

	
;;; End CIO SPECIAL ;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;

;;; Utility Functions ;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;

       ; ENABLE PROCEED INTERRUPT

ENPRCD:
	LDA     PACTL
       ORA     #$01    ; ENABLE BIT 0
       STA     PACTL
       RTS

       ; DISABLE PROCEED INTERRUPT

DIPRCD:
	LDA     PACTL
       AND     #$FE    ; DISABLE BIT0
       STA     PACTL
       RTS

       ; GET ZIOCB DEVNO - 1 INTO X
       
GDIDX:	
       LDX     ZICDNO  ; IOCB UNIT #
       DEX             ; - 1
       RTS

       ; CONVERT ZICDNO TO BUFFER
       ; PAGE, RETURN IN A

ICD2B:
	LDX	ZICDNO
	DEX
	
I3:	CPX	#$03
	BNE	I2
	LDA	#>RBUF+3
	BVC	IX

I2:	CPX	#$02
	BNE	I1
	LDA	#>RBUF+2
	BVC	IX

I1:	CPX	#$01
	BNE	I0
	LDA	#>RBUF+1
	BVC	IX

I0:	LDA	#>RBUF

IX:	RTS
	
;;; End Utility Functions ;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;

;;; Proceed Vector ;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;

PRCVEC 
       LDA     #$01
       STA     TRIP
       PLA
       RTI
	
;;; End Proceed Vector ;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;

;;; Variables

       ; DEVHDL TABLE FOR N:

CIOHND .WORD      OPEN-1
       .WORD      CLOSE-1
       .WORD      GET-1
       .WORD      PUT-1
       .WORD      STATUS-1
       .WORD      SPEC-1

       ; BANNERS
       
BREADY .BYTE      '#FUJINET READY',$9B
BERROR .BYTE      '#FUJINET ERROR',$9B

       ; VARIABLES

DSAV   .WORD      $0000
TRIP   .DS      1       ; INTR FLAG
AX1SV  .DS      MAXDEV  ; AUX1 SAVE
AX2SV  .DS      MAXDEV  ; AUX2 SAVE
STSV   .DS      4*MAXDEV ; STATUS SAVE
RLEN   .DS      MAXDEV  ; RCV LEN
ROFF   .DS      MAXDEV  ; RCV OFFSET
TOFF   .DS      MAXDEV  ; TRX OFFSET
INQDS  .DS      1       ; DSTATS INQ

       ; BUFFERS (PAGE ALIGNED)

       .ALIGN	$100

RBUF   .DS      256*MAXDEV      ; RX
TBUF   .DS      256*MAXDEV      ; TX

PGEND  =       *

	RUN	START
       END

