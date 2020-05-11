       SUBTTL  'HANDLER START'

       ORG     $2300

START  

       ; INSERT INTO HATABS

IHTBS  PROC
       
       ; FIND FIRST EMPTY ENTRY, OR
       ; ALREADY EXTANT N: ENTRY.

       LDY     #$00
:1     LDA     HATABS,Y 
       BEQ     HFND
       CMP     #'N'
       BEQ     HFND
       INY
       INY
       INY
       CPY     #11*3
       BCC     :1

       ; EITHER FOUND EMPTY SPOT,
       ; OR FOUND EXTANT N: ENTRY.

HFND   LDA     #'N'
       STA     HATABS,Y
       LDA     #LOW CIOHND
       STA     HATABS+1,Y
       LDA     #HIGH CIOHND
       STA     HATABS+2,Y
       EPROC

       ; MOVE MEMLO

       PROC
       LDA     #LOW END
       STA     MEMLO
       LDA     #HIGH END
       STA     MEMLO+1
       EPROC

       SUBTTL  'SHOW FUJINET BANNER'

       ; QUERY FUJINET

;       JSR     STPOLL

       ; OUTPUT READY OR ERROR

OBANR  PROC
       LDX     #$00    ; IOCB #0 E:
       LDA     #PUTREC
       STA     ICCOM,X
       LDA     #$28    ; 40 CHRS MAX
       STA     ICBLL,X
       LDA     #$00
       STA     ICBLH,X
       LDA     DSTATS  ; DSTATS < 128?
       BPL     OBRDY

OBERR  LDA     #LOW BERROR
       STA     ICBAL,X
       LDA     #HIGH BERROR
       STA     ICBAH,X
       JMP     OBCIO

OBRDY  LDA     #LOW BREADY
       STA     ICBAL,X
       LDA     #HIGH BREADY
       STA     ICBAH,X
OBCIO  JSR     CIOV
       EPROC

       ; VECTOR IN PROCEED INTERRUPT

SPRCED PROC
       LDA     #LOW PRCVEC
       STA     VPRCED
       LDA     #HIGH PRCVEC
       STA     VPRCED+1
       EPROC

       ; RTS BACK TO DOSVEC, DONE.

       RTS

       SUBTTL  'CIO OPEN'
       
