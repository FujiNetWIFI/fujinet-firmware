       SUBTTL  'CIO OPEN'
       
OPEN   PROC 

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
       EPROC

