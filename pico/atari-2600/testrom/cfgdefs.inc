; cfgdefs.inc -- what all three CONFIG banks agree on.
;
; Equates only, so this is included BEFORE the ORG, next to fujinet.inc.
;
; The banks do not share code, they share ZERO PAGE. A bank switch replaces
; every byte of $1000-$17FF -- there is no fixed code region to put a common
; library in, because the high 2K is the mailbox in its entirety -- so each
; bank carries its own copy of fujilib and fujidisp and the only thing that
; crosses is these variables and whatever the cartridge is holding.
;
; What the cartridge is holding matters more than what is here: the working
; directory is 256 bytes in the cart (see FN_HOT_PATH_CH), so descending into
; a subfolder costs this console nothing at all.

; The bank trampoline, at a FIXED address because the banks are assembled
; separately and each has to be able to call it by number. It is the first
; thing in the fixed tail, so cfgtail.asm ORGs here and the address cannot
; drift away from this equate.
CFGOTO  EQU     FNTAIL

BANKHST EQU     0               ; host slots
BANKDIR EQU     1               ; the directory browser
BANKINF EQU     2               ; adapter info

; $80-$A6 is the swap stub's landing ground and the transport's scratch;
; $A8-$B3 is ours. The stack runs down from $FF and must stay above $C0.
CFSEL   EQU     $AA             ; cursor row on the current screen
CFCNT   EQU     $AB             ; rows actually populated
CFHOST  EQU     $AC             ; the mounted host slot
CFPAGE  EQU     $AD             ; directory page, NROWS entries each
CFDEPTH EQU     $AE             ; how far we have descended
CFERR   EQU     $AF             ; the last FNE*
CFSTEP  EQU     $B0             ; where it went wrong, for the error line
CFWANT  EQU     $B1             ; entry index being sought while re-reading
CFFULL  EQU     $B2             ; the last page filled: paging forward is legal
CFKEY   EQU     $B3             ; this frame's newly-pressed input

HOSTSLN EQU     32              ; MAX_HOSTNAME_LEN: the stride in the reply
NHOSTS  EQU     8               ; MAX_HOST_SLOTS
DEVSLOT EQU     0               ; this console has one thing to mount into
NAMELN  EQU     30              ; READ_DIR_ENTRY maxlen. NOT 31: at exactly 31
                                ;   the firmware prepends icon bytes.
NROWS   EQU     14              ; list rows
ROW0    EQU     2               ; the first of them
ROWERR  EQU     18              ; where a failure is reported
