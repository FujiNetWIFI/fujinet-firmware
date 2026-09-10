; ---------------------------------------------------------------------------
; hello.asm -- M0: prove the toolchain, the cart header and the text renderer.
;
; No mailbox, no cartridge firmware. Runs as a plain Videocart under MAME's
; stock channelf driver, so it isolates "can we put readable text on this
; screen" from everything else.
; ---------------------------------------------------------------------------
	CPU F3850

	ORG 0800H
	DB 55H			; cart signature -- the BIOS compares this at $0800
	DB 00H			; $0801 is skipped; the BIOS jumps straight to $0802

ENTRY:	DI

	; The BIOS has already zeroed all 64 scratchpad registers and set r59
	; (the K-stack pointer) to 40, so BIOS calls are safe from here.

	LI CVAL0		; clear to value 0 = the palette's background
	LR 3,A
	PI BCLRSCR

	LI PAL2A		; light-grey ground, blue/red/green text
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
	DCI MSG1
	PI DSTR

	LI CVAL2
	LR 3,A
	LI DORGX
	LR 1,A
	LI DORGY+DCELLH
	LR 2,A
	DCI MSG2
	PI DSTR

	LI CVAL3
	LR 3,A
	LI DORGX
	LR 1,A
	LI DORGY+2*DCELLH
	LR 2,A
	DCI MSG3
	PI DSTR

	; Full-width ruler: proves 23 cells fit inside the safe area.
	LI CVAL1
	LR 3,A
	LI DORGX
	LR 1,A
	LI DORGY+4*DCELLH
	LR 2,A
	DCI MSG4
	PI DSTR

	LI CVAL2
	LR 3,A
	LI DORGX
	LR 1,A
	LI DORGY+5*DCELLH
	LR 2,A
	DCI MSG5
	PI DSTR

	; Bottom row: confirms 9 rows clear the visible area.
	LI CVAL3
	LR 3,A
	LI DORGX
	LR 1,A
	LI DORGY+8*DCELLH
	LR 2,A
	DCI MSG6
	PI DSTR

HALT:	BR HALT

MSG1:	DB "FUJINET CHANNEL F",0
MSG2:	DB "M0 Display Test",0
MSG3:	DB "abcdefg lower + UPPER",0
MSG4:	DB "01234567890123456789012",0
MSG5:	DB "!\"#$%&'()*+,-./:;<=>?@",0
MSG6:	DB "ROW 9 OF 9 -- 23 COLS",0

	INCLUDE "fujidisp.inc"
	INCLUDE "font.inc"

	END
