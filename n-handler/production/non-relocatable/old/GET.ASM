       SUBTTL  'CIO GET'

GET    PROC

       JSR     GDIDX
       LDA     RLEN,X
       BNE     :CDISC  ; LEN > 0?

       ; LEN=0, DO A STATUS POLL
       ; AND UPDATE LEN

       JSR     STPOLL
       JSR     GDIDX
       LDA     DVSTAT
       STA     RLEN,X

       ; IF LEN=0, THEN RET EOF
       ; OTHERWISE, GET DATA FROM
       ; SIO.

       BNE     :SIGET
       LDY     #EOF
       LDA     #EOF
       RTS

:SIGET LDA     #DEVIDN ; $71
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

:CDISC LDA     DVSTAT+2 ; CHECK DISC
       BNE     :EOF2   ; NOPE.
       LDA     #EOF
       LDY     #EOF
       RTS

:EOF2  LDA     RLEN,X
       BNE     :UPDP

:UPDP  DEC     RLEN,X
       LDY     ROFF,X
       CPX     #$03
       BEQ     :G3
       CPX     #$02
       BEQ     :G2
       CPX     #$01
       BEQ     :G1

       ; RETURN NEXT CHAR IN APROPOS
       ; BUFFER INTO A

:G0    LDA     RBUF,Y
       JMP     :GX
:G1    LDA     RBUF+$100,Y
       JMP     :GX
:G2    LDA     RBUF+$200,Y
       JMP     :GX
:G3    LDA     RBUF+$300,Y
       JMP     :GX
:GX    INC     ROFF,X
       TAY

       ; RESET TRIP IF LEN=0

       LDA     RLEN,X
       BNE     :DONE
       LDA     #$00
       STA     TRIP
       
:DONE  TYA
       LDY     #$01    ; SUCCESS

       RTS             ; DONE...
       EPROC

