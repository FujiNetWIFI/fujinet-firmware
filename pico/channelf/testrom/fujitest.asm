; ---------------------------------------------------------------------------
; fujitest.asm -- M1: one FujiNet round trip, on screen.
;
; Burned to a plain EPROM with no cart hardware behind it, it shows
; NO FUJINET CART together with the two bytes it read instead of the magic --
; which proves the header, the toolchain and the whole screen path on real
; iron with no link at all. On the FujiNet cart (or the MAME model) it runs
; GET_ADAPTERCONFIG_EXTENDED and prints the SSID, firmware version and IP.
;
; The last line is the point of the test: SEQ is the sequence number the cart
; acknowledged, and it comes from the CART's persisted ACKSEQ, not from a
; counter here. Reset the console and it reads 02, then 03 -- proof the client
; survives a reset that the cartridge never sees. (This console has no reset
; line on the connector at all.)
;
; Build: ../build.sh fujitest
; ---------------------------------------------------------------------------
	CPU F3850

; --- client RAM, in the arena at $8000 ---
VHEX	EQU 08000H		; 3 bytes: two hex digits and a NUL

	ORG 0800H
	DB 55H			; cart signature -- the BIOS compares this at $0800
	DB 00H			; $0801 is skipped; the BIOS jumps straight to $0802

ENTRY:	DI

	; The BIOS has already zeroed all 64 scratchpad registers and set r59
	; (the K-stack pointer) to 40, so BIOS calls are safe from here.

	LI CVAL0
	LR 3,A
	PI BCLRSCR
	LI PAL2A
	LR 3,A
	LI PAL2B
	LR 4,A
	PI DPAL

	LI CVAL1
	LR 3,A
	LI DORGX
	LR 1,A
	LI DORGY
	LR 2,A
	DCI TSTR
	PI DSTR

	PI FNCHK
	BZ FOUND

; ---- no cartridge magic: the bare-EPROM path -------------------------------
	LI CVAL2
	LR 3,A
	LI DORGX
	LR 1,A
	LI DORGY+2*DCELLH
	LR 2,A
	DCI NCSTR
	PI DSTR

	; show what was actually sitting where the magic should be
	LI DORGX
	LR 1,A
	LI DORGY+3*DCELLH
	LR 2,A
	DCI FNMAG0
	LM
	LR 5,A			; DSTR clobbers r0-r8, so stash both bytes first
	DCI FNMAG1
	LM
	LR 6,A
	LR A,5
	LR 0,A			; DHEXS takes its byte in r0: A never survives a PI
	DCI VHEX
	PI DHEXS
	DCI VHEX
	PI DSTR
	LR A,6
	LR 0,A
	DCI VHEX
	PI DHEXS
	DCI VHEX
	PI DSTR
HALT1:	BR HALT1

; ---- cartridge present: ask the adapter about itself -----------------------
FOUND:	LI FD_FUJI
	LR 0,A
	LI FC_ADPTX
	LR 1,A
	CLR
	LR 2,A			; no parameters, no payload
	PI FNBEG
	PI FNGO
	CI 0
	BZ GOTCFG

	; the transaction failed: show the error code
	LR 5,A
	LI CVAL2
	LR 3,A
	LI DORGX
	LR 1,A
	LI DORGY+2*DCELLH
	LR 2,A
	DCI ERSTR
	PI DSTR
	LR A,5
	LR 0,A
	DCI VHEX
	PI DHEXS
	DCI VHEX
	PI DSTR
HALT2:	BR HALT2

; The reply carries everything we show in one piece: ssid at +0, fn_version at
; +125, sLocalIP at +140 (AdapterConfigExtended, lib/device/fujiDevice.h).
; sLocalIP is dotted-decimal TEXT in the reply, not binary octets.
GOTCFG:	LI CVAL1
	LR 3,A
	LI DORGX
	LR 1,A
	LI DORGY+2*DCELLH
	LR 2,A
	DCI FNREPLY
	PI DSTR

	LI CVAL3
	LR 3,A
	LI DORGX
	LR 1,A
	LI DORGY+3*DCELLH
	LR 2,A
	DCI FNREPLY+125
	PI DSTR

	LI CVAL2
	LR 3,A
	LI DORGX
	LR 1,A
	LI DORGY+4*DCELLH
	LR 2,A
	DCI FNREPLY+140
	PI DSTR

	; SEQ, straight from the cart's persisted ACKSEQ
	LI CVAL1
	LR 3,A
	LI DORGX
	LR 1,A
	LI DORGY+6*DCELLH
	LR 2,A
	DCI SQSTR
	PI DSTR
	LR A,9			; FNGO left the sequence it used here
	LR 0,A
	DCI VHEX
	PI DHEXS
	DCI VHEX
	PI DSTR

HALT3:	BR HALT3

TSTR:	DB "FUJITEST CHANNEL F",0
NCSTR:	DB "NO FUJINET CART",0
ERSTR:	DB "ERR ",0
SQSTR:	DB "SEQ ",0

	INCLUDE "fujidisp.inc"
	INCLUDE "fujilib.inc"
	INCLUDE "font.inc"

; The claim signature, at the very top of the 16K window ($47FC). Only an
; exactly-16K image reaches it, so no commercial Videocart can carry it by
; accident; the cart reads it to decide whether the mailbox survives a boot.
	ORG 0800H+FN_ROM_CLAIM
	DB "FUJI"

	END
