; fujinet.inc -- equates for the FujiNet cartridge mailbox.
;
; Hand-mirrored from firmware/include/fuji_mailbox.h; keep the two in step.
;
; Split from fujilib.inc because that file contains CODE, and code has to be
; assembled inside the ORG'd program. The equates have to come first, so they
; live here. (Including the code half early instead assembles the whole
; transport at address $0000 and every JSR to it becomes `20 00 00` -- which
; assembles cleanly and fails at run time.)

; ---------------- cart -> console, painted ----------------
FNTEXT  EQU     $1800           ; six 128-byte text planes
FNRPLY  EQU     $1B00           ; the reply window, 512 bytes
FNACKS  EQU     $1F00           ; echoes SEQ when the reply is ready
FNSTAT  EQU     $1F01           ; bit0 link up, bit1 busy
FNERR   EQU     $1F02           ; FNE* below
FNRCMD  EQU     $1F03           ; $06 ACK / $15 NAK
FNRXLO  EQU     $1F04           ; reply length, little-endian
FNRXHI  EQU     $1F05
FNBST   EQU     $1F06           ; boot state
FNBPC   EQU     $1F07           ; boot percent
FNBER   EQU     $1F08           ; boot error
FNMAG0  EQU     $1F09           ; 'F'
FNMAG1  EQU     $1F0A           ; 'N'
FNPVER  EQU     $1F0B
FNSECH  EQU     $1F0C           ; slice echo, published last after a repaint
FNBTXG  EQU     $1F0D           ; last text row rendered, plus a toggle
FNBBNK  EQU     $1F0E           ; the live bank
FNBFLG  EQU     $1F0F           ; bit0 armed, bit1 claim honoured
FNPLNL  EQU     $1F17           ; path buffer length, little-endian
FNPLNH  EQU     $1F18

; ---------------- console -> cart, write-only ----------------
; NEVER READ THESE PAGES. The cart does not drive them -- not driving them is
; exactly what lets it sample the data bus -- so a read returns the floating
; bus, and worse, is decoded as an access.
FNRSEL  EQU     $1D00           ; + n (n < $80): arm register n
FNCMT   EQU     $1DFF           ; commit the armed register with this data
FNTX    EQU     $1E00           ; a write anywhere in the page appends

; Registers, unchanged from the sibling ports so fujimail.c compiles verbatim.
FR_DEV  EQU     $00
FR_CMD  EQU     $01
FR_NPAR EQU     $02
FR_DRST EQU     $05             ; any value: rewind the TX write pointer
FR_RXSL EQU     $06
FR_SEQ  EQU     $10             ; nonzero and != ACKSEQ: launch
FR_BLCK EQU     $11             ; FNBLKM: arm the ROM swap

; One-shot operations, in the bit7-set half of the control page.
FH_BANK EQU     $80
FH_TROW EQU     $F0
FH_TCHR EQU     $F1
FH_TEND EQU     $F2
FH_PATHC EQU    $F3             ; append A to the cartridge path buffer
FH_PATHO EQU    $F4             ; act on it: FP_* below
FP_RST  EQU     0
FP_POP  EQU     1
FP_TX   EQU     2
FP_TXRAW EQU    3
FH_ARM1 EQU     $FC
FH_ARM2 EQU     $FD
FH_SWAP EQU     $FE

FNAM1   EQU     $B5             ; the arming pair
FNAM2   EQU     $4A
FNBLKM  EQU     $B5             ; bootlock

FNTCOL  EQU     12              ; text columns

; $1F20-$1FFB is the client's fixed tail: the cold stub, the bank trampolines,
; and anything else that must be at the same address from every bank.
FNTAIL  EQU     $1F20

; Devices and commands, from include/fujiCommandID.h and fujiDeviceID.h.
FNDEVF  EQU     $70             ; the FUJI device
FNDEVN  EQU     $71             ; N1:
FNCADPX EQU     $C4             ; GET_ADAPTERCONFIG_EXTENDED
FNCMHST EQU     $F9             ; MOUNT_HOST
FNCMIMG EQU     $F8             ; MOUNT_IMAGE
FNCSDFP EQU     $E2             ; SET_DEVICE_FULLPATH
FNCRHST EQU     $F4             ; READ_HOST_SLOTS
FNCODIR EQU     $F7             ; OPEN_DIRECTORY
FNCRDIR EQU     $F6             ; READ_DIR_ENTRY
FNCCDIR EQU     $F5             ; CLOSE_DIRECTORY
FNCSDPS EQU     $E4             ; SET_DIRECTORY_POSITION

FMREAD  EQU     1               ; disk access mode: read

; FN_R_BOOT_STATE values.
FNBIDL  EQU     0
FNBXFR  EQU     1
FNBRDY  EQU     2
FNBFAIL EQU     $80

FNEOK   EQU     0
FNEWAIT EQU     $FF             ; the cart never answered at all
FNENAK  EQU     $EE             ; the server said NAK
FNENOC  EQU     $FD             ; no cartridge answered at all -- deliberately
                                ;   not 1, which is FN_ERR_NOLINK

; ---------------- zero page ----------------
FNSEQ   EQU     $80             ; the sequence we are waiting for
FNCNT   EQU     $83             ; timeout quanta remaining
FNDEV   EQU     $86             ; transaction setup, read by FNBEG
FNCMD   EQU     $87
FNNPR   EQU     $88
FNTMO   EQU     $89             ; timeout in ~0.6s quanta
FNPTRL  EQU     $8A
FNPTRH  EQU     $8B
FNPCNT  EQU     $8E             ; bytes written into the current path payload
INCUR   EQU     $A8             ; input, above the swap stub's landing ground
INPREV  EQU     $A9


; ---------------- input ----------------
; Joystick 0 in SWCHA bits 7-4 and the console switches in SWCHB, all active
; low, folded by INSCAN into one active-high byte.
IN_UP   EQU     $01
IN_DOWN EQU     $02
IN_LEFT EQU     $04
IN_RIGHT EQU    $08
IN_FIRE EQU     $10
IN_SEL  EQU     $20
IN_RST  EQU     $40
