       SUBTTL  'CIO PUT'

PUT    PROC
     
       ; ADD TO TX BUFFER
       
       JSR     GDIDX
       LDY     TOFF,X  ; GET OFFSET
       STA     TBUF,Y  ; STORE CHAR
       INC     TOFF,X  ; INC OFFSET
       LDY     #$01    ; SUCCESSFUL

       ; FLUSH IF EOL OR FULL

       CMP     #EOL    ; EOL?
       BEQ     :FLUSH  ; FLUSH BUFFER
       JSR     GDIDX   ; GET OFFSET
       LDY     TOFF,X
       CPX     #$FF    ; LEN = $FF?
       BEQ     :FLUSH  ; FLUSH BUFFER
       RTS

       ; FLUSH BUFFER, IF ASKED.

:FLUSH JSR     PFLUSH  ; FLUSH BUFFER
       RTS
       EPROC

PFLUSH PROC

       ; CHECK CONNECTION, AND EOF
       ; IF DISCONNECTED.

       JSR     STPOLL  ; GET STATUS
       LDA     DVSTAT+2
       BNE     :1   
       LDY     #EOF
       LDA     #EOF
       RTS

:1     JSR     GDIDX   ; GET DEV X
       LDA     TOFF,X
       BNE     :2
       JMP     :DONE

       ; FILL OUT DCB FOR PUT FLUSH

:2     LDA     #DEVIDN
       STA     DDEVIC
       LDA     ZICDNO
       STA     DUNIT
       LDA     #'W'    ; WRITE
       STA     DCOMND
       LDA     #DSWRIT
       STA     DSTATS

       ; PICK APROPOS BUFFER PAGE
       
       CPX     #$03
       BEQ     :TB3
       CPX     #$02
       BEQ     :TB2
       CPX     #$01
       BEQ     :TB1

:TB0   LDA     #HIGH TBUF
       BVC     :TBX
:TB1   LDA     #HIGH TBUF+1
       BVC     :TBX
:TB2   LDA     #HIGH TBUF+2
       BVC     :TBX
:TB3   LDA     #HIGH TBUF+3
       BVC     :TBX

       ; FINISH DCB AND DO SIOV

:TBX   STA     DBUFH
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

:DONE  LDY     #$01
       RTS
       EPROC

