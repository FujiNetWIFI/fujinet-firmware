; ---------------------------------------------------------------------------
; fujibank.asm -- APPBANK pages and the tick, on screen.
;
; A banked image: the usual 16K, then two 8K pages. It selects each page with
; one store to $FD80+n, reads the page's id byte back through the window at
; $4800, calls into the page (which hands back a string of its own), then
; selects a page the image does not have, which must read $FF. Last, it shows
; the cart's 60 Hz tick, live, for as long as it runs.
;
; Results for emu/bankprobe.lua, in RAM:
;   VRES+0  the id byte read at $4800 with page 0 selected   (want $A0)
;   VRES+1  ...with page 1 selected                          (want $A1)
;   VRES+2  FNPVER                                           (want $02)
;   VRES+3  $5A once the page tests are done
;   VRES+4  the byte at $4800 with page 2, which is missing  (want $FF)
;
; Build: ../build.sh fujibank
; ---------------------------------------------------------------------------
	CPU F3850

VHEX	EQU 08000H		; "HHLL",0
VRES	EQU 08010H

	ORG 0800H
	DB 55H			; cart signature
	DB 00H			; $0801 is skipped

ENTRY:	DI
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
	LI CVAL2
	LR 3,A
	LI DORGX
	LR 1,A
	LI DORGY+2*DCELLH
	LR 2,A
	DCI NCSTR
	PI DSTR
HALT1:	BR HALT1

FOUND:	DCI FNPVER		; the protocol version, for the probe and the eye
	LM
	DCI VRES+2
	ST
	LI CVAL2
	LR 3,A
	LI DORGX
	LR 1,A
	LI DORGY+1*DCELLH
	LR 2,A
	DCI PVSTR
	PI DSTR			; leaves r1 just past the text
	DCI VRES+2
	LM
	LR 0,A
	DCI VHEX
	PI DHEXS
	DCI VHEX
	PI DSTR

	; --- page 0: select, read its id, run its code ---
	DCI FNBANK+0
	ST			; any value selects
	DCI FN_WIN
	LM
	DCI VRES+0
	ST
	LI CVAL1
	LR 3,A
	LI DORGX
	LR 1,A
	LI DORGY+2*DCELLH
	LR 2,A
	PI 04802H		; the page's entry: DC0 = its own string
	PI DSTR

	; --- page 1 ---
	DCI FNBANK+1
	ST
	DCI FN_WIN
	LM
	DCI VRES+1
	ST
	LI CVAL3
	LR 3,A
	LI DORGX
	LR 1,A
	LI DORGY+3*DCELLH
	LR 2,A
	PI 04802H
	PI DSTR

	; --- page 2 does not exist: the window must read $FF ---
	DCI FNBANK+2
	ST
	DCI FN_WIN
	LM
	DCI VRES+4
	ST
	DCI FNBANK+1		; and back to a real page
	ST

	LI 05AH
	DCI VRES+3
	ST

	LI CVAL1
	LR 3,A
	LI DORGX
	LR 1,A
	LI DORGY+5*DCELLH
	LR 2,A
	DCI TKSTR
	PI DSTR

; --- the tick, live: read HI, LO, HI, and retry if HI moved ---
TLOOP:	DCI FNTICK+1
	LM
	LR 5,A
	DCI FNTICK
	LM
	LR 6,A
	LM			; DC0 is on FNTICK+1 again
	XS 5
	BNZ TLOOP
	LR A,5
	LR 0,A
	DCI VHEX
	PI DHEXS
	LR A,6
	LR 0,A
	DCI VHEX+2
	PI DHEXS
	LI 4
	LR 0,A
	LI DORGX+5*DCELLW
	LR 1,A
	LI DORGY+5*DCELLH
	LR 2,A
	PI DCLRR
	LI CVAL2
	LR 3,A
	LI DORGX+5*DCELLW
	LR 1,A
	LI DORGY+5*DCELLH
	LR 2,A
	DCI VHEX
	PI DSTR
	JMP TLOOP

TSTR:	DB "FUJIBANK CHANNEL F",0
NCSTR:	DB "NO FUJINET CART",0
PVSTR:	DB "PROTO ",0
TKSTR:	DB "TICK",0

	INCLUDE "fujidisp.inc"
	INCLUDE "fujilib.inc"
	INCLUDE "font.inc"

	ORG 0800H+FN_ROM_CLAIM
	DB "FUJI"

; --- page 0: image offset $4000, console $4800 ---
	ORG 04800H
	DB 0A0H, 05FH		; id, ~id
P0ENT:	DCI P0STR
	POP
P0STR:	DB "PAGE 0 RAN",0
	ORG 04800H+FN_PGSZ-1
	DB 0A0H

; --- page 1: image offset $6000, served at $4800 ---
	ORG 06800H
	PHASE 04800H
	DB 0A1H, 05EH
P1ENT:	DCI P1STR
	POP
P1STR:	DB "PAGE 1 RAN",0
	DEPHASE
	ORG 06800H+FN_PGSZ-1
	DB 0A1H

	END
