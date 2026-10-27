; ---------------------------------------------------------------------------
; fujitest.asm -- one FujiNet round trip, on screen.
;
; On a plain EPROM it shows NO FUJINET CART and the two bytes read instead of
; the magic. On the FujiNet cart (or the MAME model) it runs
; GET_ADAPTERCONFIG_EXTENDED and prints the SSID, firmware version and IP.
;
; SEQ comes from the cart's persisted ACKSEQ: each console reset shows it one
; higher.
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

; AdapterConfigExtended reply: ssid at +0, fn_version at +125, sLocalIP at
; +140 as dotted-decimal text.
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

; The claim signature at $47FC keeps the mailbox alive after a boot.
	ORG 0800H+FN_ROM_CLAIM
	DB "FUJI"

	END
