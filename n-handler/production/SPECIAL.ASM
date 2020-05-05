       SUBTTL  'CIO SPECIAL'

SPEC   PROC

       ; HANDLE LOCAL COMMANDS.

       LDA     ZICCOM
       CMP     #$0F    ; 15 = FLUSH
       BNE     :1      ; NO.
       JSR     PFLUSH  ; DO FLUSH
       LDY     #$01    ; SUCCESS
       RTS

       ; HANDLE SIO COMMANDS.
       ; GET DSTATS FOR COMMAND

:1     LDA     #DEVIDN ; $71
       STA     DDEVIC
       LDA     ZICDNO  ; UNIT #
       STA     DUNIT
       LDA     #$FF    ; DS INQ
       STA     DCOMND
       LDA     #DSREAD
       STA     DSTATS
       LDA     #LOW INQDS
       STA     DBUFL
       LDA     #HIGH INQDS
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
:DSERR TAY             ; RET THE ERR
       RTS

       ; WE GOT A DSTATS INQUIRY
       ; IF $FF, THE COMMAND IS
       ; INVALID

:DSOK  LDA     INQDS
       CMP     #$FF    ; INVALID?
       BNE     :DSGO   ; DO THE CMD
       LDY     #$92    ; UNIMP CMD
       TYA
       RTS

:DSGO  LDA     #DEVIDN ; $71
       STA     DDEVIC
       LDA     ZICCOM
       STA     DCOMND
       LDA     INQDS
       STA     DSTATS
       LDA     #HIGH TBUF
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
       EPROC

