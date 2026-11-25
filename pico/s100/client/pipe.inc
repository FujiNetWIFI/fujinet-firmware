;=====================================================================
; PIPE.INC -- the S-100 card's pipe settings, then pico/common's FNPORT.INC.
;=====================================================================

FNBASE  EQU     0A0h            ; the card's default DIP setting
FNSKIP1 EQU     08h             ; a scan skips the FDC
FNSKIP2 EQU     10h             ; and a 2SIO console

        INCLUDE "fnport.inc"
