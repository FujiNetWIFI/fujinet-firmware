	;; N: Device Handler

       ; CURRENT IOCB IN ZERO PAGE

ZIOCB  EQU     $20      ; ZP IOCB
ZICHID EQU     ZIOCB    ; ID
ZICDNO EQU     ZIOCB+1  ; UNIT #
ZICCOM EQU     ZIOCB+2  ; COMMAND
ZICSTA EQU     ZIOCB+3  ; STATUS
ZICBAL EQU     ZIOCB+4  ; BUF ADR LOW
ZICBAH EQU     ZIOCB+5  ; BUF ADR HIGH
ZICPTL EQU     ZIOCB+6  ; PUT ADDR L
ZICPTH EQU     ZIOCB+7  ; PUT ADDR H
ZICBLL EQU     ZIOCB+8  ; BUF LEN LOW
ZICBLH EQU     ZIOCB+9  ; BUF LEN HIGH
ZICAX1 EQU     ZIOCB+10 ; AUX 1
ZICAX2 EQU     ZIOCB+11 ; AUX 2
ZICAX3 EQU     ZIOCB+12 ; AUX 3
ZICAX4 EQU     ZIOCB+13 ; AUX 4
ZICAX5 EQU     ZIOCB+14 ; AUX 5
ZICAX6 EQU     ZIOCB+15 ; AUX 6

       ; INTERRUPT VECTORS
       ; AND OTHER PAGE 2 VARS

VPRCED EQU     $0202   ; PROCEED VCTR
COLOR2 EQU     $02C6   ; MODEF BKG C
MEMLO  EQU     $02E7   ; MEM LO
DVSTAT EQU     $02EA   ; 4 BYTE STATS

       ; PAGE 3
       ; DEVICE CONTROL BLOCK (DCB)

DCB    EQU     $0300   ; BASE
DDEVIC EQU     DCB     ; DEVICE #
DUNIT  EQU     DCB+1   ; UNIT #
DCOMND EQU     DCB+2   ; COMMAND
DSTATS EQU     DCB+3   ; STATUS/DIR
DBUFL  EQU     DCB+4   ; BUF ADR L
DBUFH  EQU     DCB+5   ; BUF ADR H
DTIMLO EQU     DCB+6   ; TIMEOUT (S)
DRSVD  EQU     DCB+7   ; NOT USED
DBYTL  EQU     DCB+8   ; BUF LEN L
DBYTH  EQU     DCB+9   ; BUF LEN H
DAUXL  EQU     DCB+10  ; AUX BYTE L
DAUXH  EQU     DCB+11  ; AUX BYTE H

HATABS EQU     $031A   ; HANDLER TBL

       ; IOCB'S * 8

IOCB   EQU     $0340   ; IOCB BASE
ICHID  EQU     IOCB    ; ID
ICDNO  EQU     IOCB+1  ; UNIT #
ICCOM  EQU     IOCB+2  ; COMMAND
ICSTA  EQU     IOCB+3  ; STATUS
ICBAL  EQU     IOCB+4  ; BUF ADR LOW
ICBAH  EQU     IOCB+5  ; BUF ADR HIGH
ICPTL  EQU     IOCB+6  ; PUT ADDR L
ICPTH  EQU     IOCB+7  ; PUT ADDR H
ICBLL  EQU     IOCB+8  ; BUF LEN LOW
ICBLH  EQU     IOCB+9  ; BUF LEN HIGH
ICAX1  EQU     IOCB+10 ; AUX 1
ICAX2  EQU     IOCB+11 ; AUX 2
ICAX3  EQU     IOCB+12 ; AUX 3
ICAX4  EQU     IOCB+13 ; AUX 4
ICAX5  EQU     IOCB+14 ; AUX 5
ICAX6  EQU     IOCB+15 ; AUX 6

       ; HARDWARE REGISTERS

PACTL  EQU     $D302   ; PIA CTRL A

       ; OS ROM VECTORS

CIOV   EQU     $E456   ; CIO ENTRY
SIOV   EQU     $E459   ; SIO ENTRY

       ; CONSTANTS

PUTREC EQU     $09     ; CIO PUTREC
DEVIDN EQU     $71     ; SIO DEVID
DSREAD EQU     $40     ; FUJI->ATARI
DSWRIT EQU     $80     ; ATARI->FUJI
MAXDEV EQU     4       ; # OF N: DEVS
EOF    EQU     $88     ; ERROR 136
EOL    EQU     $9B     ; EOL CHAR

	;; INIT

	.RELOC

	.public start
	
START  

       ; INSERT INTO HATABS

IHTBS  .proc
       
       ; FIND FIRST EMPTY ENTRY, OR
       ; ALREADY EXTANT N: ENTRY.

       LDY     #$00
H1     LDA     HATABS,Y 
       BEQ     HFND
       CMP     #'N'
       BEQ     HFND
       INY
       INY
       INY
       CPY     #11*3
       BCC     H1

       ; EITHER FOUND EMPTY SPOT,
       ; OR FOUND EXTANT N: ENTRY.

HFND   LDA     #'N'
       STA     HATABS,Y
       LDA     <CIOHND
       STA     HATABS+1,Y
       LDA     >CIOHND
       STA     HATABS+2,Y
       .endp

       ; MOVE MEMLO

MML    .proc
       LDA     <END
       STA     MEMLO
       LDA     >END
       STA     MEMLO+1
       .endp

       ; QUERY FUJINET

;       JSR     STPOLL

       ; OUTPUT READY OR ERROR

OBANR  .proc
       LDX     #$00    ; IOCB #0 E:
       LDA     #PUTREC
       STA     ICCOM,X
       LDA     #$28    ; 40 CHRS MAX
       STA     ICBLL,X
       LDA     #$00
       STA     ICBLH,X
       LDA     DSTATS  ; DSTATS < 128?
       BPL     OBRDY

OBERR  LDA     <BERROR
       STA     ICBAL,X
       LDA     >BERROR
       STA     ICBAH,X
       JMP     OBCIO

OBRDY  LDA     <BREADY
       STA     ICBAL,X
       LDA     >BREADY
       STA     ICBAH,X
OBCIO  JSR     CIOV
       .endp

OPEN   .proc 

       ; PERFORM THE OPEN

       JSR     ENPRCD  ; ENABLE PRCED
       JSR     GDIDX   ; X=ZICDNO-1
       LDA     #DEVIDN ; $70
       STA     DDEVIC
       LDA     ZICDNO
       STA     DUNIT
       LDA     #'O'    ; 'O'PEN
       STA     DCOMND
       LDA     #DSWRIT ; -> PERIP
       STA     DSTATS
       LDA     ZICBAL  ; POINT DBUF
       STA     DBUFL   ; TO FILENAME
       LDA     ZICBAH  ; ...
       STA     DBUFH   ; ...
       LDA     #$0F
       STA     DTIMLO
       LDA     #$00    ; OPEN WANTS
       STA     DBYTL   ; 256 BYTE
       LDA     #$01    ; PAYLOAD
       STA     DBYTH   ; ...
       LDA     ZICAX1  ; IOCB AUX1...
       STA     AX1SV,X ; ...SAVE IT
       STA     DAUXL   ; ...USE IT
       LDA     ZICAX2  ; IOCB AUX2...
       STA     AX2SV,X ; ...SAVE IT
       STA     DAUXH   ; ...USE IT
       JSR     SIOV    ; SEND TO #FN
                                    
       ; RETURN DSTATS UNLESS = 144
       ; IN WHICH CASE, DO A STATUS
       ; AND RETURN THE EXTENDED ERR
       ; FROM IT...

OPCERR LDY     DSTATS  ; GET SIO STATUS
       CPY     #$90    ; ERR 144?
       BNE     OPDONE  ; NOPE. RETURN DSTATS
       
       ; 144, GET EXTENDED ERROR

       JSR     STPOLL  ; POLL FOR STATUS
       LDY     DVSTAT+3

       ; RESET BUFFER LENGTH + OFFSET
       
OPDONE LDA     #$01
       STA     TRIP
       JSR     GDIDX
       LDA     #$00
       STA     RLEN,X
       STA     TOFF,X
       STA     ROFF,X
       TYA
       RTS             ; AY = ERROR
       .endp


CLOSE  .proc
       JSR     DIPRCD  ; DIS INTRPS
       LDA     #DEVIDN ; $70
       STA     DDEVIC
       LDA     ZICDNO  ; UNIT #
       STA     DUNIT
       LDA     #'C'    ; C = CLOSE
       STA     DCOMND
       LDA     #$00    ; NO PAYLOAD
       STA     DSTATS
       STA     DBUFL
       STA     DBUFH
       STA     DBYTL
       STA     DBYTH
       STA     DAUXL
       STA     DAUXH
       LDA     #$0F
       STA     DTIMLO
       JSR     SIOV

       ; FLUSH BUFFERS IF NEEDED
       JSR     PFLUSH

       LDY     DSTATS
       TYA
       RTS
       .endp

GET    .proc

       JSR     GDIDX
       LDA     RLEN,X
       BNE     CDISC  ; LEN > 0?

       ; LEN=0, DO A STATUS POLL
       ; AND UPDATE LEN

       JSR     STPOLL
       JSR     GDIDX
       LDA     DVSTAT
       STA     RLEN,X

       ; IF LEN=0, THEN RET EOF
       ; OTHERWISE, GET DATA FROM
       ; SIO.

       BNE     SIGET
       LDY     #EOF
       LDA     #EOF
       RTS

SIGET LDA     #DEVIDN ; $71
       STA     DDEVIC
       LDA     ZICDNO  
       STA     DUNIT
       LDA     #'R'
       STA     DCOMND
       LDA     #DSREAD
       STA     DSTATS
       JSR     ICD2B
       STA     DBUFH
       LDA     #$00
       STA     DBUFL
       STA     DBYTH
       STA     DAUXH
       LDA     #$0F
       STA     DTIMLO
       LDA     DVSTAT
       STA     DBYTL
       STA     DAUXL
       JSR     SIOV
       JSR     GDIDX
       LDA     #$00
       STA     ROFF,X

CDISC LDA     DVSTAT+2 ; CHECK DISC
       BNE     EOF2   ; NOPE.
       LDA     #EOF
       LDY     #EOF
       RTS

EOF2  LDA     RLEN,X
       BNE     UPDP

UPDP  DEC     RLEN,X
       LDY     ROFF,X
       CPX     #$03
       BEQ     G3
       CPX     #$02
       BEQ     G2
       CPX     #$01
       BEQ     G1

       ; RETURN NEXT CHAR IN APROPOS
       ; BUFFER INTO A

G0    LDA     RBUF,Y
       JMP     GX
G1    LDA     RBUF+$100,Y
       JMP     GX
G2    LDA     RBUF+$200,Y
       JMP     GX
G3    LDA     RBUF+$300,Y
       JMP     GX
GX    INC     ROFF,X
       TAY

       ; RESET TRIP IF LEN=0

       LDA     RLEN,X
       BNE     DONE
       LDA     #$00
       STA     TRIP
       
DONE  TYA
       LDY     #$01    ; SUCCESS

       RTS             ; DONE...
       .endp

PUT    .proc
     
       ; ADD TO TX BUFFER
       
       JSR     GDIDX
       LDY     TOFF,X  ; GET OFFSET
       STA     TBUF,Y  ; STORE CHAR
       INC     TOFF,X  ; INC OFFSET
       LDY     #$01    ; SUCCESSFUL

       ; FLUSH IF EOL OR FULL

       CMP     #EOL    ; EOL?
       BEQ     FLUSH  ; FLUSH BUFFER
       JSR     GDIDX   ; GET OFFSET
       LDY     TOFF,X
       CPX     #$FF    ; LEN = $FF?
       BEQ     FLUSH  ; FLUSH BUFFER
       RTS

       ; FLUSH BUFFER, IF ASKED.

FLUSH JSR     PFLUSH  ; FLUSH BUFFER
       RTS
       .endp

PFLUSH .proc

       ; CHECK CONNECTION, AND EOF
       ; IF DISCONNECTED.

       JSR     STPOLL  ; GET STATUS
       LDA     DVSTAT+2
       BNE     L1   
       LDY     #EOF
       LDA     #EOF
       RTS

L1     JSR     GDIDX   ; GET DEV X
       LDA     TOFF,X
       BNE     L2
       JMP     DONE

       ; FILL OUT DCB FOR PUT FLUSH

L2     LDA     #DEVIDN
       STA     DDEVIC
       LDA     ZICDNO
       STA     DUNIT
       LDA     #'W'    ; WRITE
       STA     DCOMND
       LDA     #DSWRIT
       STA     DSTATS

       ; PICK APROPOS BUFFER PAGE
       
       CPX     #$03
       BEQ     TB3
       CPX     #$02
       BEQ     TB2
       CPX     #$01
       BEQ     TB1

TB0   LDA     >TBUF
       BVC     TBX
TB1   LDA     >TBUF+1
       BVC     TBX
TB2   LDA     >TBUF+2
       BVC     TBX
TB3   LDA     >TBUF+3
       BVC     TBX

       ; FINISH DCB AND DO SIOV

TBX   STA     DBUFH
       LDA     #$00
       STA     DBUFL
       STA     DBYTH
       STA     DAUXH
       LDA     #$0F
       STA     DTIMLO
       LDA     TOFF,X
       STA     DBYTL
       STA     DAUXL
       JSR     SIOV
       
       ; CLEAR THE OFFSET CURSOR
       ; AND LENGTH

       JSR     GDIDX
       LDA     #$00
       STA     TOFF,X

DONE  LDY     #$01
       RTS
       .endp

       ; IF TRIP, DO STATUS POLL
       ; OTHERWISE, RETURN SAVED
       ; STATUS...

STATUS .proc
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
       .endp

       ; ASK FUJINET FOR STATUS

STPOLL .proc
       LDA     #DEVIDN ; #FN DEVID
       STA     DDEVIC
       LDA     ZICDNO  ; IOCB #
       STA     DUNIT
       LDA     #'S'    ; S = STATUS
       STA     DCOMND
       LDA     #DSREAD ; #FN->ATARI
       STA     DSTATS
       LDA     <DVSTAT
       STA     DBUFL   ; PUT IN DVSTAT
       LDA     >DVSTAT
       STA     DBUFH   ; ...
       LDA     #$04    ; 4 BYTES
       STA     DBYTL   
       LDA     #$00    
       STA     DBYTH
       LDA     #$0F
       STA     DTIMLO
       LDA     #$00
       STA     DAUXL
       STA     DAUXH
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
       .endp

SPEC   .proc

       ; HANDLE LOCAL COMMANDS.

       LDA     ZICCOM
       CMP     #$0F    ; 15 = FLUSH
       BNE     L1      ; NO.
       JSR     PFLUSH  ; DO FLUSH
       LDY     #$01    ; SUCCESS
       RTS

       ; HANDLE SIO COMMANDS.
       ; GET DSTATS FOR COMMAND

L1     LDA     #DEVIDN ; $71
       STA     DDEVIC
       LDA     ZICDNO  ; UNIT #
       STA     DUNIT
       LDA     #$FF    ; DS INQ
       STA     DCOMND
       LDA     #DSREAD
       STA     DSTATS
       LDA     <INQDS
       STA     DBUFL
       LDA     >INQDS
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
       BPL     DSOK
DSERR TAY             ; RET THE ERR
       RTS

       ; WE GOT A DSTATS INQUIRY
       ; IF $FF, THE COMMAND IS
       ; INVALID

DSOK  LDA     INQDS
       CMP     #$FF    ; INVALID?
       BNE     DSGO   ; DO THE CMD
       LDY     #$92    ; UNIMP CMD
       TYA
       RTS

DSGO  LDA     #DEVIDN ; $71
       STA     DDEVIC
       LDA     ZICCOM
       STA     DCOMND
       LDA     INQDS
       STA     DSTATS
       LDA     >TBUF
       STA     DBUFH
       LDA     #$00
       STA     DBUFL
       STA     DBYTH
       LDA     #$0F
       STA     DTIMLO
       LDA     ZICAX1
       STA     DAUXL
       LDA     ZICAX2
       STA     DAUXH
       JSR     SIOV
       LDY     DSTATS
       TYA
       RTS
       .endp

       ; ENABLE .procEED INTERRUPT

ENPRCD LDA     PACTL
       ORA     #$01    ; ENABLE BIT 0
       STA     PACTL
       RTS

       ; DISABLE .procEED INTERRUPT

DIPRCD LDA     PACTL
       AND     #$FE    ; DISABLE BIT0
       STA     PACTL
       RTS

       ; GET ZIOCB DEVNO - 1 INTO X
       
GDIDX  .proc
       LDX     ZICDNO  ; IOCB UNIT #
       DEX             ; - 1
       RTS
       .endp

       ; CONVERT ZICDNO TO BUFFER
       ; PAGE, RETURN IN A

ICD2B  .proc
       LDX     ZICDNO
       DEX
       CPX     #$03
       BEQ     L3
       CPX     #$02
       BEQ     L2
       CPX     #$01
       BEQ     L1

L0     LDA     >RBUF
       BVC     DONE

L1     LDA     >RBUF + 1
       BVC     DONE

L2     LDA     >RBUF + 2
       BVC     DONE

L3     LDA     >RBUF + 3
       BVC     DONE

DONE  RTS
       .endp

       ; VECTOR IN .procEED INTERRUPT

SPRCED .proc
       LDA     <PRCVEC
       STA     VPRCED
       LDA     >PRCVEC
       STA     VPRCED+1
       .endp

       ; RTS BACK TO DOSVEC, DONE.

       RTS

	
PRCVEC .proc 
       LDA     #$01
       STA     TRIP
       PLA
       RTI
       .endp
                
       ; --------- END OF CODE -----

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

TRIP           DTA $00       ; INTR FLAG
AX1SV          DTA $00,$00,$00,$00  ; AUX1 SAVE
AX2SV          DTA $00,$00,$00,$00  ; AUX2 SAVE
RLEN           DTA $00,$00,$00,$00  ; RCV LEN
ROFF           DTA $00,$00,$00,$00  ; RCV OFFSET
TOFF           DTA $00,$00,$00,$00  ; TRX OFFSET
INQDS          DTA $00       ; DSTATS INQ

RBUF           DTA [255]      ; RX
TBUF           DTA [255]      ; TX

END    =       *

