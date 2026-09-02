; o2fuji.inc -- 8048 mirror of firmware/include/fuji_mailbox.h.
; Hand-synchronised; do not change one without the other.

; console -> cart: MOVX writes, external-data window (P13 high, P14 low, P16 low)
FN_W_REGSEL     equ 0E0h
FN_W_REGDATA    equ 0E1h
FN_W_DATA       equ 0E2h
FN_W_SEQ        equ 0E3h

FN_REG_DEVICE   equ 000h
FN_REG_CMD      equ 001h
FN_REG_NPARAM   equ 002h
FN_REG_TXLEN_LO equ 003h
FN_REG_TXLEN_HI equ 004h
FN_REG_DATA_RST equ 005h
FN_REG_RXSLICE  equ 006h
FN_REG_BOOTSEL  equ 007h
FN_REG_CODEBANK equ 008h
FN_REG_DATABANK equ 009h

; cart -> console: MOVP reads out of program page $F00
FN_R_ACKSEQ     equ 0F20h
FN_R_STATUS     equ 0F21h
FN_R_ERR        equ 0F22h
FN_R_REPLY_CMD  equ 0F23h
FN_R_RXLEN_LO   equ 0F24h
FN_R_RXLEN_HI   equ 0F25h
FN_R_BOOT_STATE equ 0F26h
FN_R_BOOT_PCT   equ 0F27h
FN_R_BOOT_ERR   equ 0F28h
FN_R_MAGIC0     equ 0F29h
FN_R_MAGIC1     equ 0F2Ah
FN_R_PROTO_VER  equ 0F2Bh
FN_R_DATA       equ 0F30h

FUJI_DEVICEID   equ 070h
FUJICMD_GET_ADAPTERCONFIG_EXTENDED equ 0C4h
FUJICMD_ACK     equ 006h
FUJICMD_NAK     equ 015h

; External-data window selected: P13 high (VDC off the bus), P14 low, and P16
; MUST stay low or the cartridge write strobe is gated off.
P1_SEL_XRAM equ P1_BANK0|P1_BANK1|P1_KBD|P1_VDC|P1_NC|P1_LUM
