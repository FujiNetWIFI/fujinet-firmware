       SUBTTL  'CIO CLOSE'

CLOSE  PROC
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
       EPROC

