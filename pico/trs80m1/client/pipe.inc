;=====================================================================
; PIPE.INC -- the Model I card's pipe settings, then pico/common's
; FNPORT.INC. Only the default base is tried: E8h-EBh is the RS-232
; board, where a stray read takes a received byte.
;=====================================================================

FNBASE  EQU     0D0h
FNNOSCAN EQU    1

        INCLUDE "fnport.inc"
