; fujinet.inc -- the cart's mailbox and the 7800's registers, for ca65.
; Hand-mirrored from firmware/include/fuji_mailbox.h; keep them in step.
;
; Write pages take plain STA/STX/STY only: a read-modify-write instruction
; writes the old value first, which the cart sees as a stray event.

FN_LOADER       = $0600         ; the loader (cart-served, 512 bytes)
FN_LOADER_BOOT  = $0600         ; load the armed image (or CONFIG) and start it
FN_LOADER_CONFIG = $0603        ; load CONFIG and start it
FN_REPLY        = $0800         ; 1K reply window
FN_STATUS       = $0C00         ; status page
FN_REGSEL       = $0D00         ; write V to +n: register n = V
FN_TXDATA       = $0F00         ; write anywhere: append one TX byte

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
FN_LOADSTATE    = FN_STATUS + $0D
FN_LOADDST      = FN_STATUS + $0E
FN_LOADN        = FN_STATUS + $0F
FN_LOADSEQ      = FN_STATUS + $10
FN_LOADPCT      = FN_STATUS + $11
FN_MODE         = FN_STATUS + $12
FN_MAPPER       = FN_STATUS + $14
FN_LINK         = FN_STATUS + $15
FN_BOOTGOT      = FN_STATUS + $16
FN_BOOTTOT      = FN_STATUS + $19
FN_INPTCTRL     = FN_STATUS + $1C
FN_INPTLOCK     = FN_STATUS + $1D
FN_TV           = FN_STATUS + $1E
FN_STAGED       = FN_STATUS + $1F
FN_STAGEDKIND   = FN_STATUS + $20
FN_STAGEDCRC    = FN_STATUS + $21
FN_HANDOVER     = FN_STATUS + $25
FN_HSC          = FN_STATUS + $26
FN_DIAGRMW      = FN_STATUS + $27

; registers (write to FN_REGSEL + n)
FN_REG_DEVICE   = $00
FN_REG_CMD      = $01
FN_REG_NPARAM   = $02
FN_REG_DATA_RST = $05
FN_REG_RXSLICE  = $06
FN_REG_SEQ      = $10
FN_REG_BOOTLOCK = $11
FN_REG_SLICE_ACK = $14
FN_REG_TV       = $15
FN_REG_HSC      = $16
FN_HOT_GO_BIOS  = $FB
FN_HOT_CONFIG   = $FC
FN_HOT_GO       = $FD
FN_HOT_SWAP     = $FE

FN_BOOTLOCK_MAGIC = $B5

FN_BOOT_READY   = 2
FN_BOOT_FAILED  = $80
FN_LOAD_IDLE    = 0
FN_LOAD_SLICE   = 1
FN_LOAD_DONE    = 2
FN_HO_BIOS      = 1
FN_HO_DIRECT    = 2

; the claim, at $FF70 in every FujiNet app
FN_CLAIM        = $FF70

; 7800 hardware
INPTCTRL        = $01           ; write-only; any TIA write reaches it until locked
INPT4           = $0C           ; left fire (one-button mode)
INPT5           = $0D
INPT0           = $08           ; left buttons (two-button mode)
INPT1           = $09
BACKGRND        = $20
P0C1            = $21
P0C2            = $22
P0C3            = $23
MWSYNC          = $24
P1C1            = $25
P1C2            = $26
P1C3            = $27
MSTAT           = $28           ; bit 7: VBLANK
P2C1            = $29
P2C2            = $2A
P2C3            = $2B
DPPH            = $2C
P3C1            = $2D
P3C2            = $2E
P3C3            = $2F
DPPL            = $30
P4C1            = $31
P4C2            = $32
P4C3            = $33
CHARBASE        = $34
P5C1            = $35
P5C2            = $36
P5C3            = $37
OFFSET          = $38
P6C1            = $39
P6C2            = $3A
P6C3            = $3B
CTRL            = $3C
P7C1            = $3D
P7C2            = $3E
P7C3            = $3F
SWCHA           = $280
CTLSWA          = $281
SWCHB           = $282
CTLSWB          = $283

; client error codes (fujilib.s)
FNEOK     = 0                   ; FN_ERR_OK
FNEWAIT   = $FF                 ; the cart never answered at all
FNENAK    = $EE                 ; the server said NAK
FNENOC    = $FD                 ; no cartridge answered ('F','N' missing)

; FujiNet devices and commands
FNDEVF    = $70                 ; the FUJI device
FNCADPX   = $C4                 ; GET_ADAPTERCONFIG_EXTENDED
FNCMHST   = $F9                 ; MOUNT_HOST
FNCMIMG   = $F8                 ; MOUNT_IMAGE
FNCSDFP   = $E2                 ; SET_DEVICE_FULLPATH
FMREAD    = 1                   ; disk access mode: read

; the clients' zero page, clear of what the loader uses ($80-$89)
FN_SEQ    = $E0                 ; the sequence we are waiting for
FN_CNT    = $E1                 ; timeout quanta remaining
FN_DEV    = $E2                 ; transaction setup, read by fn_beg
FN_CMD    = $E3
FN_NPR    = $E4
FN_TMO    = $E5                 ; timeout, in ~0.7 s quanta
FN_PTR    = $E6                 ; a string pointer (2 bytes)
DISP_PTR  = $E8                 ; the text cursor (2 bytes)
DISP_TMP  = $EA
