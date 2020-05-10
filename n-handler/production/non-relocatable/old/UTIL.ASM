       SUBTTL  'UTILITY FUNCTIONS'

       ; ENABLE PROCEED INTERRUPT

ENPRCD LDA     PACTL
       ORA     #$01    ; ENABLE BIT 0
       STA     PACTL
       RTS

       ; DISABLE PROCEED INTERRUPT

DIPRCD LDA     PACTL
       AND     #$FE    ; DISABLE BIT0
       STA     PACTL
       RTS

       ; GET ZIOCB DEVNO - 1 INTO X
       
GDIDX  PROC
       LDX     ZICDNO  ; IOCB UNIT #
       DEX             ; - 1
       RTS
       EPROC

       ; CONVERT ZICDNO TO BUFFER
       ; PAGE, RETURN IN A

ICD2B  PROC
       LDX     ZICDNO
       DEX
       CPX     #$03
       BEQ     :3
       CPX     #$02
       BEQ     :2
       CPX     #$01
       BEQ     :1

:0     LDA     #HIGH RBUF
       BVC     :DONE

:1     LDA     #HIGH RBUF + 1
       BVC     :DONE

:2     LDA     #HIGH RBUF + 2
       BVC     :DONE

:3     LDA     #HIGH RBUF + 3
       BVC     :DONE

:DONE  RTS
       EPROC

