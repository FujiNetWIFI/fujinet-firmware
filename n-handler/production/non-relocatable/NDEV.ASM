  
       LIST D,G,I

       TITLE   'N: CIO HANDLER'
       SUBTTL  'EQUATES'

; N: HANDLER FOR FUJINET
; AUTHOR: THOM CHERRYHOMES
;   <THOM.CHERRYHOMES@GMAIL.COM>

       INCLUDE D2:EQUATES.ASM
       INCLUDE D2:INIT.ASM
       INCLUDE D2:OPEN.ASM
       INCLUDE D2:CLOSE.ASM
       INCLUDE D2:GET.ASM
       INCLUDE D2:PUT.ASM
       INCLUDE D2:STATUS.ASM
       INCLUDE D2:SPECIAL.ASM
       INCLUDE D2:UTIL.ASM

       SUBTTL  'INITIALIZATION'

       ; PROCEED VECTOR

PRCVEC PROC 
       LDA     #$01
       STA     TRIP
       PLA
       RTI
       EPROC
                
       ; --------- END OF CODE -----

       ; DEVHDL TABLE FOR N:

CIOHND DW      OPEN-1
       DW      CLOSE-1
       DW      GET-1
       DW      PUT-1
       DW      STATUS-1
       DW      SPEC-1

       ; BANNERS
       
BREADY DB      '#FUJINET READY',$9B
BERROR DB      '#FUJINET ERROR',$9B

       ; VARIABLES

TRIP   DS      1       ; INTR FLAG
AX1SV  DS      MAXDEV  ; AUX1 SAVE
AX2SV  DS      MAXDEV  ; AUX2 SAVE
STSV   DS      4*MAXDEV ; STATUS SAVE
RLEN   DS      MAXDEV  ; RCV LEN
ROFF   DS      MAXDEV  ; RCV OFFSET
TOFF   DS      MAXDEV  ; TRX OFFSET
INQDS  DS      1       ; DSTATS INQ

       ; BUFFERS (PAGE ALIGNED)

       ORG     * + $FF & $FF00

RBUF   DS      256*MAXDEV      ; RX
TBUF   DS      256*MAXDEV      ; TX

END    =       *

       ORG     $02E0
       DW      START
