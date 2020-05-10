       SUBTTL  'CIO STATUS'

       ; IF TRIP, DO STATUS POLL
       ; OTHERWISE, RETURN SAVED
       ; STATUS...

STATUS PROC
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
       EPROC

       ; ASK FUJINET FOR STATUS

STPOLL PROC
       LDA     #DEVIDN ; #FN DEVID
       STA     DDEVIC
       LDA     ZICDNO  ; IOCB #
       STA     DUNIT
       LDA     #'S'    ; S = STATUS
       STA     DCOMND
       LDA     #DSREAD ; #FN->ATARI
       STA     DSTATS
       LDA     #LOW DVSTAT
       STA     DBUFL   ; PUT IN DVSTAT
       LDA     #HIGH DVSTAT
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
       EPROC

