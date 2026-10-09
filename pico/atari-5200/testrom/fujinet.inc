; fujinet.inc -- the cart's mailbox and the 5200's registers, for ca65.
; Hand-mirrored from firmware/include/fuji_mailbox.h; tools/checkdefs.py
; compares them.
;
; Console -> cart is reads only: `lda FN_REGSEL+n` then `lda FN_REGDATA,y`
; with the value in Y. Hotspot pages take LDA/LDX/LDY/BIT abs or abs,X/Y
; from a page-aligned base, never a store, an RMW or (zp),Y.

FN_REPLY        = $B000         ; 1K reply window
FN_STATUS       = $B400         ; status page
FN_REGSEL       = $B500         ; read +n: select register n
FN_REGDATA      = $B600         ; read +v: the selected register = v
FN_TXDATA       = $B700         ; read +b: append one TX byte
FN_STUB         = $B5F0         ; cart-served JMP ($FFFC); swaps when armed

; status page (FN_R_* - $400)
FN_ACKSEQ       = FN_STATUS + $00
FN_STAT         = FN_STATUS + $01
FN_ERR          = FN_STATUS + $02
FN_REPLYCMD     = FN_STATUS + $03
FN_RXLEN        = FN_STATUS + $04
FN_BOOTSTATE    = FN_STATUS + $06
FN_BOOTPCT      = FN_STATUS + $07
FN_BOOTERR      = FN_STATUS + $08
FN_MAGIC        = FN_STATUS + $09   ; 'F','N'
FN_PROTOVER     = FN_STATUS + $0B
FN_MODE         = FN_STATUS + $12
FN_MAPPER       = FN_STATUS + $14
FN_LINK         = FN_STATUS + $15
FN_BOOTGOT      = FN_STATUS + $16
FN_BOOTTOT      = FN_STATUS + $19
FN_ARMED        = FN_STATUS + $1C
FN_STAGED       = FN_STATUS + $1D
FN_STAGEDKIND   = FN_STATUS + $1E
FN_STAGEDCRC    = FN_STATUS + $1F
FN_SWAPS        = FN_STATUS + $23

; registers (read FN_REGSEL + n, then FN_REGDATA + value)
FN_REG_DEVICE   = $00
FN_REG_CMD      = $01
FN_REG_NPARAM   = $02
FN_REG_DATA_RST = $05
FN_REG_RXSLICE  = $06
FN_REG_SEQ      = $10
FN_REG_BOOTLOCK = $11
FN_REG_BOOTSEL_1 = $12
FN_REG_BOOTSEL_2 = $13
FN_REG_CONFIG   = $14

FN_BOOTLOCK_MAGIC = $B5
FN_CONFIG_MAGIC = $C5

FN_BOOT_READY   = 2
FN_BOOT_FAILED  = $80
FN_MODE_BOOT    = 0
FN_MODE_GAME    = 2
FN_MODE_APP     = 3

; the claim, at $BFE0 in every FujiNet app
FN_CLAIM        = $BFE0

; 5200 hardware
GTIA            = $C000
COLPM0          = $C012
COLPF0          = $C016
COLPF1          = $C017
COLPF2          = $C018
COLPF3          = $C019
COLBK           = $C01A
PRIOR           = $C01B
GRACTL          = $C01D
CONSOL          = $C01F
TRIG0           = $C010
PAL             = $C014
ANTIC           = $D400
DMACTL          = $D400
CHACTL          = $D401
DLISTL          = $D402
DLISTH          = $D403
PMBASE          = $D407
CHBASE          = $D409
WSYNC           = $D40A
VCOUNT          = $D40B
NMIEN           = $D40E
NMIST           = $D40F
NMIRES          = $D40F
POKEY           = $E800
AUDF1           = $E800
AUDC1           = $E801
AUDCTL          = $E808
POT0            = $E800
KBCODE          = $E809
RANDOM          = $E80A
POTGO           = $E80B
IRQEN           = $E80E
IRQST           = $E80E
SKCTL           = $E80F
SKSTAT          = $E80F

; the BIOS's zero page, which a52init.s keeps the same
POKMSK          = $00
RTCLOKH         = $01
RTCLOKL         = $02
CRITIC          = $03
ATRACT          = $04
SDLSTL          = $05
SDLSTH          = $06
SDMCTL          = $07
PCOLR0          = $08
COLOR0          = $0C
COLOR1          = $0D
COLOR2          = $0E
COLOR3          = $0F
COLOR4          = $10
PADDL0          = $11

; ...and its vectors
VIMIRQ          = $0200
VVBLKI          = $0202
VVBLKD          = $0204
VDSLST          = $0206
VKYBDI          = $0208
VKYBDF          = $020A
VTRIGR          = $020C
VBRKOP          = $020E
VSERIN          = $0210
VTIMR4          = $021A

; client error codes (fujilib.s)
FNEOK     = 0                   ; FN_ERR_OK
FNEWAIT   = $FF                 ; the cart never answered at all
FNENAK    = $EE                 ; the server said NAK
FNENOC    = $FD                 ; no cartridge answered ('F','N' missing)
FNENARM   = $FC                 ; the swap was never armed

; FujiNet devices and commands
FNDEVF    = $70                 ; the FUJI device
FNCADPX   = $C4                 ; GET_ADAPTERCONFIG_EXTENDED
FNCMHST   = $F9                 ; MOUNT_HOST
FNCMIMG   = $F8                 ; MOUNT_IMAGE
FNCSDFP   = $E2                 ; SET_DEVICE_FULLPATH
FMREAD    = 1                   ; disk access mode: read

; the clients' zero page, clear of the BIOS's $00-$18
KEY       = $19                 ; last keypad code + 1, 0 = none (a52init.s)
FN_SEQ    = $E0                 ; the sequence we are waiting for
FN_CNT    = $E1                 ; frames left before giving up (2 bytes)
FN_DEV    = $E3                 ; transaction setup, read by fn_beg
FN_CMD    = $E4
FN_NPR    = $E5
FN_TMO    = $E6                 ; timeout, in seconds
FN_LAST   = $E7                 ; RTCLOKL as fn_wait last saw it
FN_PTR    = $E8                 ; a string pointer (2 bytes)
DISP_PTR  = $EA                 ; the text cursor (2 bytes)
DISP_TMP  = $EC

SCREEN    = $1000               ; 40 x 24, mode 2
