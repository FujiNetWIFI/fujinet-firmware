; HVGLIB.H -- Home Video Game Library equates for the Bally Astrocade,
; FujiNet subset.
;
; An original transcription of the facts in "Software and Hardware for the
; Bally Arcade: A Technical Description" (Dave Nutting Associates, 1978):
; system-call indexes, hardware ports, on-board ROM cells and SENTRY codes.
; Only what the FujiNet clients use is here; it keeps the community file's
; names so sources read like every other BallyAlley-convention program.
; zmac syntax.

; ---- User Program Interface -------------------------------------------
; A system call is RST 56 followed by one byte: routine index + option.
; Option 0 takes arguments in registers; option 1 (the "suck" bit) reads
; them inline after the call byte.

SUCK    EQU     1               ; inline-arguments option bit

SYSTEM  macro   callno          ; args in registers
        RST     56
        DB      callno
        endm

SYSSUK  macro   callno          ; args follow inline
        RST     56
        DB      callno+SUCK
        endm

; Inside the interpreter (entered with SYSTEM INTPC) each call is one byte:
DO      macro   callno          ; interpreter call, args inline
        DB      callno+SUCK
        endm

DONT    macro   callno          ; interpreter call, args in registers
        DB      callno
        endm

EXIT    macro                   ; leave the interpreter
        DB      XINTC
        endm

; ---- System-call routine indexes --------------------------------------
INTPC   EQU     00H             ; begin interpreting
XINTC   EQU     02H             ; exit interpreter
RCALL   EQU     04H             ; call assembly subroutine
MCALL   EQU     06H             ; call interpreter subroutine
MRET    EQU     08H             ; return from interpreter subroutine
MJUMP   EQU     0AH             ; interpreter jump
ACTINT  EQU     0EH             ; activate interrupts
DECCTS  EQU     10H             ; decrement counter/timers
SETOUT  EQU     16H             ; set display ports (VERBL, HORCB, INMOD)
COLSET  EQU     18H             ; set color registers
FILL    EQU     1AH             ; fill area with constant
RECTAN  EQU     1CH             ; paint a rectangle
WRITR   EQU     20H             ; write pattern, relative
WRIT    EQU     24H             ; write pattern
BLANK   EQU     2AH             ; blank area
CHRDIS  EQU     32H             ; display character
STRDIS  EQU     34H             ; display string (0-terminated)
DISNUM  EQU     36H             ; display BCD number
KCTASC  EQU     40H             ; key code to ASCII
SENTRY  EQU     42H             ; sense transition
DOIT    EQU     44H             ; respond to input transition
PIZBRK  EQU     48H             ; coffee break
MENU    EQU     4AH             ; display menu, branch on selection
GETPAR  EQU     4CH             ; get game parameter
GETNUM  EQU     4EH             ; get number
PAWS    EQU     50H             ; pause
MOVE    EQU     5EH             ; move bytes
RANGED  EQU     76H             ; ranged random number
MSKTD   EQU     7EH             ; joystick mask to deltas

; ---- Output ports ------------------------------------------------------
COL0R   EQU     00H             ; color registers, right palette
COL1R   EQU     01H
COL2R   EQU     02H
COL3R   EQU     03H
COL0L   EQU     04H             ; color registers, left palette
COL1L   EQU     05H
COL2L   EQU     06H
COL3L   EQU     07H
CONCM   EQU     08H             ; consumer/commercial (low/high res)
HORCB   EQU     09H             ; horizontal color boundary
VERBL   EQU     0AH             ; vertical blank line
MAGIC   EQU     0CH             ; magic register
INFBK   EQU     0DH             ; interrupt feedback
INMOD   EQU     0EH             ; interrupt enable and mode
INLIN   EQU     0FH             ; interrupt line
TONMO   EQU     10H             ; master oscillator
VOLC    EQU     15H             ; tone C volume, noise mode
VOLAB   EQU     16H             ; tone A/B volumes
VOLN    EQU     17H             ; noise volume
XPAND   EQU     19H             ; expand register

; ---- Input ports -------------------------------------------------------
INTST   EQU     08H             ; intercept feedback
VERAF   EQU     0EH             ; vertical address feedback
HORAF   EQU     0FH             ; horizontal address feedback
SW0     EQU     10H             ; player 1 handle
SW1     EQU     11H
SW2     EQU     12H
SW3     EQU     13H
KEY0    EQU     14H             ; keypad column 0 (right)
KEY1    EQU     15H
KEY2    EQU     16H
KEY3    EQU     17H             ; keypad column 3 (left)
POT0    EQU     1CH             ; player 1 knob
POT1    EQU     1DH
POT2    EQU     1EH
POT3    EQU     1FH

; ---- Memory map --------------------------------------------------------
FIRSTC  EQU     2000H           ; first byte of cartridge ROM
NORMEM  EQU     4000H           ; screen RAM (normal, non-magic image)
BYTEPL  EQU     40              ; bytes per screen line
BITSPL  EQU     160             ; pixels per screen line

; ---- On-board ROM cells ------------------------------------------------
MENUST  EQU     0218H           ; head of the on-board menu chain
ALKEYS  EQU     0214H           ; keypad mask: all 24 keys

; ---- SENTRY return codes (register B) ---------------------------------
SNUL    EQU     00H             ; nothing happened
SSEC    EQU     11H             ; seconds timer counted down
SKYU    EQU     12H             ; key up
SKYD    EQU     13H             ; key down
ST0     EQU     14H             ; player 1 trigger
SJ0     EQU     15H             ; player 1 joystick
SP0     EQU     1CH             ; player 1 knob
