; ---------------------------------------------------------------------------
; fujilib.inc -- the console half of the FujiNet mailbox, in F8 assembly.
;
; Hand-mirrors firmware/include/fuji_mailbox.h; keep the two in step.
;
; Console -> cart is an ordinary STORE on this machine, not a read hotspot,
; because ROMC 05 is a real write cycle. That makes the client dramatically
; simpler than its Astrocade/Arcadia/Coleco siblings:
;
;   - a register write is ONE store. The address picks the register, the data
;     is the value; the cart synthesises the REGSEL/REGDATA pair that the
;     shared fujimail.c decodes.
;   - registers 0, 1 and 2 are consecutive, so DEVICE, CMD and NPARAM go out
;     as three STs off a single DCI, riding DC0's auto-increment.
;   - the TX stream appends on a write ANYWHERE in its page, so a payload is
;     one DCI followed by a run of STs.
;
; THE ONE RULE THAT WILL BITE: the TX page is 256 bytes and DC0 auto-increments
; past it into $0000, which is BIOS ROM and silently swallows stores. So never
; run more than 256 STs from a single DCI FNTX. Re-issue FNTXP between the
; parameter bytes and a 256-byte payload -- otherwise the tail of the payload
; is dropped and the command NAKs for no visible reason.
;
; Register use: FNGO owns r9. FNBEG takes r0/r1/r2. Nothing here calls
; anything else, because the F8 keeps exactly one return address.
; ---------------------------------------------------------------------------

; --- painted status page ($FC00) ---
FNACKSQ	EQU 0FC00H		; echoes SEQ when the reply is ready
FNSTATE	EQU 0FC01H		; bit0 link up, bit1 busy
FNERR	EQU 0FC02H		; FN_ERR_* of the last transaction
FNRCMD	EQU 0FC03H		; $06 ACK / $15 NAK
FNRXLO	EQU 0FC04H		; reply length, little-endian
FNRXHI	EQU 0FC05H
FNBSTAT	EQU 0FC06H		; boot state
FNBPCT	EQU 0FC07H		; boot progress, 0-100
FNBERR	EQU 0FC08H
FNMAG0	EQU 0FC09H		; 'F'
FNMAG1	EQU 0FC0AH		; 'N'
FNPVER	EQU 0FC0BH
FNSECHO	EQU 0FC0CH

FNREPLY	EQU 0F800H		; the whole 1K reply, read in place
FNRSIZE	EQU 0400H

; --- the ROM window, and the claim that keeps the mailbox alive across a boot ---
FN_ROM_BASE  EQU 00800H
FN_ROM_SIZE  EQU 04000H		; 16K
FN_ROM_CLAIM EQU 03FFCH		; image offset of "FUJI"; console $47FC

; --- console -> cart ---
FNREG	EQU 0FD00H		; store V at +n: register n = V
FNSWAP	EQU 0FDFEH		; store anything: serve the staged image
FNTX	EQU 0FF00H		; store anywhere in the page: append

; --- register numbers ---
FR_DEV	EQU 000H
FR_CMD	EQU 001H
FR_NPAR	EQU 002H
FR_DRST	EQU 005H
FR_SLIC	EQU 006H
FR_SEQ	EQU 010H
FR_BLOK	EQU 011H
FR_BSL1	EQU 012H
FR_BSL2	EQU 013H

FN_BLKM	EQU 0B5H		; BOOTLOCK magic

; --- boot states ---
FNB_IDL	EQU 0
FNB_XFR	EQU 1
FNB_RDY	EQU 2
FNB_FAIL EQU 080H

; --- error codes ---
FNE_OK	EQU 0
FNE_NOL	EQU 1
FNE_TMO	EQU 2
FNE_BAD	EQU 3
FNE_BIG	EQU 4

; --- FujiBus devices and the commands this port uses ---
FD_FUJI	EQU 070H
FD_NET	EQU 071H

FC_ADPTX EQU 0C4H		; GET_ADAPTERCONFIG_EXTENDED
FC_MHOST EQU 0F9H		; MOUNT_HOST
FC_MIMG	EQU 0F8H		; MOUNT_IMAGE
FC_SDFP	EQU 0E2H		; SET_DEVICE_FULLPATH
FC_RHOST EQU 0F4H		; READ_HOST_SLOTS
FC_WHOST EQU 0F3H		; WRITE_HOST_SLOTS
FC_ODIR	EQU 0F7H		; OPEN_DIRECTORY
FC_RDIR	EQU 0F6H		; READ_DIR_ENTRY
FC_CDIR	EQU 0F5H		; CLOSE_DIRECTORY
FC_SDPOS EQU 0E4H		; SET_DIRECTORY_POSITION
FC_SSSID EQU 0FBH		; SET_SSID
FC_SCAN	EQU 0FDH		; SCAN_NETWORKS
FC_SCANR EQU 0FCH		; GET_SCAN_RESULT
FC_WIFIS EQU 0FAH		; GET_WIFISTATUS
FC_WIFIE EQU 0EAH		; GET_WIFI_ENABLED
FC_GSSID EQU 0FEH		; GET_SSID
FC_COPY	EQU 0D8H		; COPY_FILE

; ---------------------------------------------------------------------------
; FNCHK -- is a FujiNet cart answering? Returns with Z set if so.
;   Clobbers A, DC0.
; ---------------------------------------------------------------------------
FNCHK:	DCI FNMAG0
	LM
	CI 'F'
	BNZ FNCHKX
	LM			; DC0 already advanced to FNMAG1
	CI 'N'
FNCHKX:	POP

; ---------------------------------------------------------------------------
; FNTXP -- point DC0 at the TX stream. Call before every run of at most 256
;   stores; see the rule at the top of this file.
;   Clobbers DC0.
; ---------------------------------------------------------------------------
FNTXP:	DCI FNTX
	POP

; ---------------------------------------------------------------------------
; FNBEG -- start building a transaction.
;   r0 = device, r1 = command, r2 = nparam.
;   Writes DEVICE/CMD/NPARAM (three consecutive registers, so three stores off
;   one DCI), rewinds the TX write pointer, and leaves DC0 on the TX page so
;   the caller can ST its parameter and payload bytes straight in.
;   Clobbers A, DC0.
; ---------------------------------------------------------------------------
FNBEG:	DCI FNREG+FR_DEV
	LR A,0
	ST			; register 0 = device
	LR A,1
	ST			; register 1 = command
	LR A,2
	ST			; register 2 = nparam
	DCI FNREG+FR_DRST
	CLR
	ST			; register 5, any value: rewind TX
	DCI FNTX
	POP

; ---------------------------------------------------------------------------
; FNGO -- commit the transaction and wait for the reply.
;   Returns A = FN_ERR_* of the transaction. Clobbers A, r9, DC0.
;
;   The sequence number comes from the cart's OWN persisted ACKSEQ + 1, never
;   from a counter in RAM. A console RESET restarts this program and re-zeroes
;   its variables but does NOT reset the cart -- there is no reset line on the
;   connector at all -- so a local counter would replay a sequence the cart has
;   already acknowledged and the client would read a stale reply as a fresh
;   one. Every port in this family learned that the same way.
; ---------------------------------------------------------------------------
FNGO:	DCI FNACKSQ
	LM			; A = the cart's last acknowledged sequence
	INC
	BNZ FNGO1
	LIS 1			; 0 is reserved as "never used": wrap 255 -> 1
FNGO1:	LR 9,A
	DCI FNREG+FR_SEQ
	ST			; launching the transaction is this single store
FNGO2:	DCI FNACKSQ
	LM
	XS 9
	BNZ FNGO2		; ACKSEQ is published LAST, after the whole reply
	DCI FNERR
	LM
	POP

; ---------------------------------------------------------------------------
; FNRXL -- reply length into r0 (low) and r1 (high).
;   Clobbers A, DC0.
; ---------------------------------------------------------------------------
FNRXL:	DCI FNRXLO
	LM
	LR 0,A
	LM
	LR 1,A
	POP
