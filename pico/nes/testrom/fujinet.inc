; fujinet.inc -- equates for the FujiNet cartridge mailbox on the NES.
;
; Hand-mirrored from firmware/include/fuji_mailbox.h; keep the two in step.
; Console address = $5000 + arena offset.
;
; THE ONE RULE: only STA/STX/STY to $5500-$57FF, plain or indexed. Never an
; RMW instruction (INC/DEC/ASL/LSR/ROL/ROR abs): the 6502 writes the old value
; back first, and that lands as a spurious event. tools/checkrom.py rejects it.

; ---------------- cart -> console, painted ----------------
FN_RPLY   = $5000               ; the 1K reply window (also the loader's slices)
FN_ACKS   = $5400               ; echoes SEQ when the reply is ready
FN_STAT   = $5401               ; bit0 link up, bit1 busy
FN_ERR    = $5402               ; FNE* below
FN_RCMD   = $5403               ; $06 ACK / $15 NAK
FN_RXLO   = $5404               ; reply length, little-endian
FN_RXHI   = $5405
FN_BST    = $5406               ; boot state
FN_BPC    = $5407               ; boot percent
FN_BER    = $5408               ; boot error
FN_MAG0   = $5409               ; 'F'
FN_MAG1   = $540A               ; 'N'
FN_PVER   = $540B
FN_SECH   = $540C               ; slice echo
FN_LSTATE = $540D               ; loader: FN_L* below
FN_LDST   = $540E               ; loader: 0 PRG, 1 CHR
FN_LOFF   = $540F               ; loader: 1K index in the 8K window, 0-7
FN_LSEQ   = $5410               ; loader: bumps per slice, published last
FN_LPCT   = $5411
FN_SRAMST = $5412               ; 1 once the '595 has SRAM_EN on
FN_DRMW   = $5413               ; RMW dummy writes the cart dropped
FN_MAPPER = $5414
FN_LINK   = $5415

; ---------------- console -> cart, write-only ----------------
FN_RSEL   = $5500               ; write V to +n (n < $80): register n = V
FN_RDAT   = $5600               ; raw REGDATA half; unused by clients
FN_TX     = $5700               ; a write anywhere in the page appends
FN_LOADER = $5800               ; the loader ROM: JMP here to boot a staged image

; Registers, unchanged from the sibling ports.
FR_DEV    = $00
FR_CMD    = $01
FR_NPAR   = $02
FR_DRST   = $05                 ; any value: rewind the TX write pointer
FR_RXSL   = $06
FR_SEQ    = $10                 ; nonzero and != ACKSEQ: launch
FR_BLCK   = $11                 ; FN_BLKM: arm the image load
FR_BSEL1  = $12
FR_BSEL2  = $13
FR_SACK   = $14                 ; loader: slice copied

FH_SWAP   = $FE                 ; one-shot: begin loading the staged image
FN_BLKM   = $B5                 ; bootlock magic

; Boot state.
FN_BIDL   = 0
FN_BXFR   = 1
FN_BRDY   = 2
FN_BFAIL  = $80

; Loader state.
FN_LIDLE  = 0
FN_LSLICE = 1
FN_LDONE  = 2
FN_LFAIL  = $80

; Errors, as fn_go / fn_ack return them in A.
FNEOK     = 0                   ; FN_ERR_OK
FNEWAIT   = $FF                 ; the cart never answered at all
FNENAK    = $EE                 ; the server said NAK
FNENOC    = $FD                 ; no cartridge answered ('F','N' missing)

; Devices and commands, from include/fujiDeviceID.h and fujiCommandID.h.
FNDEVF    = $70                 ; the FUJI device
FNDEVN    = $71                 ; N1:
FNCADPX   = $C4                 ; GET_ADAPTERCONFIG_EXTENDED
FNCMHST   = $F9                 ; MOUNT_HOST
FNCMIMG   = $F8                 ; MOUNT_IMAGE
FNCSDFP   = $E2                 ; SET_DEVICE_FULLPATH
FNCRHST   = $F4                 ; READ_HOST_SLOTS
FNCODIR   = $F7                 ; OPEN_DIRECTORY
FNCRDIR   = $F6                 ; READ_DIR_ENTRY
FNCCDIR   = $F5                 ; CLOSE_DIRECTORY
FNCSDPS   = $E4                 ; SET_DIRECTORY_POSITION
FMREAD    = 1                   ; disk access mode: read

; ---------------- zero page the library owns ($E0-$EF) ----------------
FN_SEQ    = $E0                 ; the sequence we are waiting for
FN_CNT    = $E1                 ; timeout quanta remaining
FN_DEV    = $E2                 ; transaction setup, read by fn_beg
FN_CMD    = $E3
FN_NPR    = $E4
FN_TMO    = $E5                 ; timeout, in ~0.7 s quanta
FN_PTR    = $E6                 ; a string pointer (2 bytes)
FN_PCNT   = $E8                 ; bytes in the current path payload

; PPU and APU, for the clients.
PPU_CTRL   = $2000
PPU_MASK   = $2001
PPU_STATUS = $2002
PPU_OAMADDR = $2003
PPU_SCROLL = $2005
PPU_ADDR   = $2006
PPU_DATA   = $2007
APU_DMC    = $4010
APU_STATUS = $4015
APU_FRAME  = $4017
JOY1       = $4016
