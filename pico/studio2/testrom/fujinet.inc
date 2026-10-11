; fujinet.inc -- the 1802 side of the FujiNet Studio II cartridge: names.
;
; Mirrors firmware/include/fuji_mailbox.h and s2_text.h by hand;
; tools/checkdefs.py compares them. Equates only: include before any ORG.

; ---- registers, and the conventions every client keeps ----
R0      EQU     0       ; DMA pointer: only the ISR sets it
R1      EQU     1       ; interrupt PC (isr)
R2      EQU     2       ; stack, top at STACK, X = 2 outside s2call.inc
R3      EQU     3       ; PC
R4      EQU     4       ; SCRT call  (FCALL)
R5      EQU     5       ; SCRT return (FRET)
R6      EQU     6       ; SCRT link
R7      EQU     7       ; SCRT jump  (FJMP)
R8      EQU     8       ; scratch: any routine may clobber R8, R9
R9      EQU     9
RA      EQU     10      ; RA-RC: kept across calls
RB      EQU     11
RC      EQU     12
RD      EQU     13      ; .0 beep frames (ISR), .1 SCRT's D while it runs
RE      EQU     14      ; frames since power-on (ISR)
RF      EQU     15      ; hotspot pointer: macros only, parked on FN_PARK

HI      FUNCTION x,((x)>>8)&0FFH
LO      FUNCTION x,(x)&0FFH

STACK   EQU     09FFH   ; console RAM is $0800-$09FF, all the app's
RAMVARS EQU     0800H   ; s2call.inc's and fujilib.inc's variables

; ---- the arena (fuji_mailbox.h) ----
FN_REPLY        EQU     0E400H          ; 1K reply window
FN_STATUS       EQU     0E800H          ; status page
FN_REGSEL       EQU     0E900H          ; read +n: select register n
FN_REGDATA      EQU     0EA00H          ; read +v: the selected register = v
FN_TXDATA       EQU     0EB00H          ; read +b: append one TX byte
FN_STUB         EQU     0E9F0H          ; SEX RC; RET; $23 -- run with P = C
FN_PARK         EQU     0BH             ; RF.1 between hotspot reads: inert
FN_RASTER       EQU     0F400H          ; 64x128, 8 bytes a line

FN_ACKSEQ       EQU     FN_STATUS+00H
FN_STAT         EQU     FN_STATUS+01H
FN_ERR          EQU     FN_STATUS+02H
FN_REPLYCMD     EQU     FN_STATUS+03H
FN_RXLEN        EQU     FN_STATUS+04H   ; LE
FN_BOOTSTATE    EQU     FN_STATUS+06H
FN_BOOTPCT      EQU     FN_STATUS+07H
FN_BOOTERR      EQU     FN_STATUS+08H
FN_MAGIC        EQU     FN_STATUS+09H   ; 'F','N'
FN_PROTOVER     EQU     FN_STATUS+0BH
FN_MODE         EQU     FN_STATUS+12H
FN_LINK         EQU     FN_STATUS+15H
FN_ARMED        EQU     FN_STATUS+1CH
FN_STAGED       EQU     FN_STATUS+1DH
FN_SWAPS        EQU     FN_STATUS+23H
FN_FRAMES       EQU     FN_STATUS+24H

FN_REG_DEVICE   EQU     00H
FN_REG_CMD      EQU     01H
FN_REG_NPARAM   EQU     02H
FN_REG_DATA_RST EQU     05H
FN_REG_RXSLICE  EQU     06H
FN_REG_SEQ      EQU     10H
FN_REG_BOOTLOCK EQU     11H
FN_REG_BOOTSEL_1 EQU    12H
FN_REG_BOOTSEL_2 EQU    13H
FN_REG_CONFIG   EQU     14H
FN_BOOTLOCK_MAGIC EQU   0B5H
FN_CONFIG_MAGIC EQU     0C5H

FN_BOOT_IDLE    EQU     0
FN_BOOT_XFER    EQU     1
FN_BOOT_READY   EQU     2
FN_BOOT_FAILED  EQU     80H
FN_MODE_BOOT    EQU     0
FN_MODE_GAME    EQU     2
FN_MODE_APP     EQU     3
FN_CLAIM        EQU     07FCH           ; "FUJI"

; ---- the text engine (s2_text.h) ----
TX_CURSOR       EQU     0ECH            ; page: read +rc
TX_PUTC         EQU     0EDH            ; page: read +char
TX_OP           EQU     0EEH            ; page: read +op
TX_PARAM        EQU     0EFH            ; page: read +byte
TOP_CLS         EQU     00H
TOP_INV_ON      EQU     01H
TOP_INV_OFF     EQU     02H
TOP_CLREOL      EQU     03H
TOP_SCROLL      EQU     04H
TOP_PRINT       EQU     10H
TOP_PRINT_PAD   EQU     11H
TOP_MARQUEE     EQU     20H
TOP_MQ_OFF      EQU     21H
TOP_REPEAT      EQU     30H
TOP_GLYPH       EQU     40H
SYM_SPADE       EQU     01H
SYM_HEART       EQU     02H
SYM_DIAMOND     EQU     03H
SYM_CLUB        EQU     04H
SYM_BLOCK       EQU     05H
SYM_RIGHT       EQU     06H
SYM_LEFT        EQU     07H
SYM_UP          EQU     08H
SYM_DOWN        EQU     09H
SYM_TEN         EQU     0AH
SYM_BACK        EQU     0BH

; ---- FujiBus (lib/device/fujiDevice) ----
FNDEVF          EQU     70H             ; the FUJI device
FNCADPX         EQU     0C4H            ; GET_ADAPTERCONFIG_EXTENDED
FNCMHST         EQU     0F9H            ; MOUNT_HOST
FNCMIMG         EQU     0F8H            ; MOUNT_IMAGE
FNCSDFP         EQU     0E2H            ; SET_DEVICE_FULLPATH
FMREAD          EQU     1               ; disk access mode: read

; ---- what fujilib.inc returns in D ----
FNEOK           EQU     0               ; FN_ERR_OK
FNEWAIT         EQU     0FFH            ; the cart never answered at all
FNENAK          EQU     0EEH            ; the server said NAK
FNENOC          EQU     0FDH            ; no cartridge answered ('F','N' missing)
FNENARM         EQU     0FCH            ; the swap was never armed

; ---- the keypads (input.inc) ----
KEY_A           EQU     0A0H            ; pad A key k: KEY_A + k
KEY_B           EQU     0B0H            ; pad B key k: KEY_B + k
