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
FUJICMD_MOUNT_HOST                 equ 0F9h
FUJICMD_MOUNT_IMAGE                equ 0F8h
FUJICMD_SET_DEVICE_FULLPATH        equ 0E2h
FUJICMD_ACK     equ 006h
FUJICMD_NAK     equ 015h

FUJICMD_READ_HOST_SLOTS            equ 0F4h
FUJICMD_OPEN_DIRECTORY             equ 0F7h
FUJICMD_READ_DIR_ENTRY             equ 0F6h
FUJICMD_CLOSE_DIRECTORY            equ 0F5h
FUJICMD_SET_DIRECTORY_POSITION     equ 0E4h

MODE_READ       equ 1

; Internal RAM. $00-$07 is register bank 0 and $08-$17 is the stack, so the
; scratch this client owns starts at $20.
V_SLICE  equ 020h           ; which reply slice is currently published
V_HOST   equ 021h           ; highlighted host slot
V_POS    equ 022h           ; directory entry index
V_STATE  equ 023h
V_STAT   equ 028h           ; 4 bytes: the quad-row status cell
V_NAME   equ 030h           ; NAMELEN bytes: the entry name being displayed
NAMELEN  equ 12             ; display width, and all the RAM a name gets
PATHLEN  equ 64             ; full length asked for when building a boot path;
                            ; must stay inside one 128-byte reply slice

; Layout. Two rows, and the roles are forced by pitch, not by preference:
; quad characters are drawn at DOUBLE pitch -- 16 pixels apart, not 8 -- so the
; 16 of them would span 256 pixels on a 160-pixel screen. Only about ten are
; ever visible. So the 12 single characters carry the filename at tight pitch,
; and one quad group carries a 4-character status cell.
QY       equ 24             ; quad row: the status cell
QX       equ 16
NX       equ 32             ; single-character row: the name
QDELTA   equ (CMAP_YP-QY)/2 ; character-map bias correction for the quad row

; External-data window selected: P13 high (VDC off the bus), P14 low, and P16
; MUST stay low or the cartridge write strobe is gated off.
P1_SEL_XRAM equ P1_BANK0|P1_BANK1|P1_KBD|P1_VDC|P1_NC|P1_LUM
