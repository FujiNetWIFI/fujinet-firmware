; ---------------------------------------------------------------------------
; fujidisp.inc -- Channel F display primitives.
;
; VRAM is 128x64 at 2bpp, WRITE-ONLY, reachable only through I/O ports:
;   port 1  bits 7-6  pixel value, inverted on the wire
;   port 4  bits 6-0  column, inverted
;   port 5  bits 5-0  row, inverted; bits 7-6 are SOUND and must be preserved
;   port 0  bit 5     write strobe, pulsed $60 -> $50, then a settling delay
;
; The port sequence here is transcribed from the console BIOS's own plotter
; ($0718-$0765), not from documentation -- including the read-modify-write on
; port 5 that keeps the sound bits, and the 4-iteration settle after each
; strobe. Getting either wrong silently corrupts sound or drops pixels.
;
; Visible area is ~102x58 from (4,4); the safe area on a real set is ~95x58.
; A 4x6 cell over that gives 23 columns x 9 rows.
;
; Colour constants are the ves.h convention: the value sits in bits 7-6 and is
; complemented on the way to port 1, so port value = ~r3 and VRAM pixel = r3>>6.
; ---------------------------------------------------------------------------

; A pixel is 2 bits. What colour those 2 bits actually show depends on the
; row's palette, so these are named by VALUE, not by colour -- the ves.h names
; GREEN/RED/BLUE/BKG only describe palette 3 and are misleading everywhere else.
CVAL0	EQU 000H
CVAL1	EQU 040H
CVAL2	EQU 080H
CVAL3	EQU 0C0H

; Palette is selected per row by the pixel values in columns 125 and 126:
;   index = ((col126 & 2) | (col125 >> 1)),  colours = colormap[index*4 + value]
; Only bit 1 of each column matters, so CVAL0 and CVAL2 are the two choices.
;   palette 0  value0 BLACK    value1-3 all WHITE      <- monochrome, max contrast
;   palette 1  value0 LTBLUE   1 BLUE  2 RED  3 GREEN
;   palette 2  value0 LTGRAY   1 BLUE  2 RED  3 GREEN  <- CONFIG default
;   palette 3  value0 LTGREEN  1 BLUE  2 RED  3 GREEN
; Pass these as (r3, r4) to DPAL:
PAL0A	EQU CVAL0
PAL0B	EQU CVAL0
PAL1A	EQU CVAL2
PAL1B	EQU CVAL0
PAL2A	EQU CVAL0
PAL2B	EQU CVAL2
PAL3A	EQU CVAL2
PAL3B	EQU CVAL2

BCLRSCR	EQU 000D0H		; BIOS clear screen, colour in r3 bits 7-6

; --- geometry -------------------------------------------------------------
DORGX	EQU 4			; left edge of the visible area
DORGY	EQU 4			; top edge
DCELLW	EQU 4			; 3px glyph + 1px gap
DCELLH	EQU 6			; 5px glyph + 1px gap
DCOLS	EQU 23			; cells across the safe area
DROWS	EQU 9			; cells down

; ---------------------------------------------------------------------------
; DPAL -- set the palette on all 64 rows.
;   r3 = pixel value for column 125, r4 = value for column 126 (see PALnA/B).
; Must run AFTER a screen clear: the BIOS clear paints columns 125/126 too and
; would otherwise pick the palette for us. Columns 125/126 sit outside the
; visible area, so writing them costs nothing on screen.
; Clobbers A, r2, r5, r8.
; ---------------------------------------------------------------------------
DPAL:	CLR
	LR 2,A
DPALLP:	LR A,3
	COM
	OUTS 1
	LR A,2
	COM
	NI 3FH
	LR 8,A
	INS 5
	NI 0C0H
	AS 8
	OUTS 5
	LI 125
	COM
	OUTS 4
	LI 60H
	OUTS 0
	LI 50H
	OUTS 0
	LIS 4
	LR 5,A
DPALD1:	DS 5
	BNZ DPALD1

	LR A,4
	COM
	OUTS 1
	LI 126
	COM
	OUTS 4
	LI 60H
	OUTS 0
	LI 50H
	OUTS 0
	LIS 4
	LR 5,A
DPALD2:	DS 5
	BNZ DPALD2

	LR A,2
	INC
	LR 2,A
	CI 63
	BP DPALLP
	POP

; ---------------------------------------------------------------------------
; DPLOT -- plot one pixel. Leaf routine: returns with POP, so it may only be
; reached by PI from a routine that is not itself using PC1.
;   r1 = column, r2 = row, r3 = colour.   Clobbers A, r8.
; ---------------------------------------------------------------------------
DPLOT:	LR A,3
	COM
	OUTS 1
	LR A,2
	COM
	NI 3FH
	LR 8,A
	INS 5
	NI 0C0H			; keep the sound bits the client may be driving
	AS 8
	OUTS 5
	LR A,1
	COM
	OUTS 4
	LI 60H
	OUTS 0			; strobe high
	LI 50H
	OUTS 0			; strobe low -- the write lands here
	LIS 4
	LR 8,A
DPDLY:	DS 8
	BNZ DPDLY
	POP

; ---------------------------------------------------------------------------
; DSTR -- draw a NUL-terminated string.
;   DC0 -> string, r1 = left pixel column, r2 = top pixel row, r3 = colour.
; Advances 4 pixels per cell. Clobbers A, r0, r4-r8, DC0, DC1.
;
; Deliberately flat -- the glyph walk and the pixel write are inlined rather
; than called, because the F8 keeps exactly one return address (PC1) and
; nesting would need the BIOS K-stack in the inner loop.
;
; Lowercase folds to uppercase and anything outside $20-$5F becomes '?'.
; ---------------------------------------------------------------------------
DSTR:	LR A,3
	COM
	OUTS 1			; colour is constant for the whole string

DSTRLP:	LM			; A = char, DC0++
	CI 0
	BZ DSTRZ

	CI 60H
	BP DSNOLC		; $60 >= A: not lowercase
	CI 7AH
	BM DSNOLC		; A > $7A: not lowercase
	AI 0E0H			; -$20, fold to uppercase
DSNOLC:	CI 1FH
	BP DSBAD		; A <= $1F
	CI 5FH
	BM DSBAD		; A > $5F
	BR DSOK
DSBAD:	LI 3FH			; '?'
DSOK:	AI 0E0H			; -FNTFST: glyph index 0..63
	LR 0,A

	XDC			; park the string pointer in DC1
	DCI FNTR0
	LR A,0
	ADC			; DC0 = FNTR0 + index
	LIS 5
	LR 7,A			; 5 glyph rows
	CLR
	LR 6,A			; row offset within the cell

DSROW:	LM			; A = this row's 3 bits; DC0++
	SL 4
	SL 1			; bit 2 -> bit 7
	LR 5,A
	LI 63
	ADC			; DC0 += 63: same glyph, next row table

	LR A,2
	AS 6
	COM
	NI 3FH
	LR 8,A
	INS 5
	NI 0C0H
	AS 8
	OUTS 5			; VRAM row, sound bits preserved

	LR A,1
	LR 4,A			; r4 = running column
	LIS 3
	LR 8,A			; 3 pixels per row -- r8 is free until the strobe

DSPIX:	LR A,5
	OI 0			; LR A,r does not set flags; this does
	BP DSSKIP		; S set = bit 7 clear = pixel off
	LR A,4
	COM
	OUTS 4
	LI 60H
	OUTS 0
	LI 50H
	OUTS 0
	LIS 4
	LR 0,A
DSDLY:	DS 0
	BNZ DSDLY

DSSKIP:	LR A,5
	SL 1
	LR 5,A
	LR A,4
	INC
	LR 4,A
	DS 8
	BNZ DSPIX

	LR A,6
	INC
	LR 6,A
	DS 7
	BNZ DSROW

	XDC			; string pointer back into DC0
	LR A,1
	AI DCELLW
	LR 1,A
	BR DSTRLP

DSTRZ:	POP

; ---------------------------------------------------------------------------
; DHEXS -- format r0 as two hex digits plus a NUL, at DC0.
;   The caller then points DC0 at the same buffer and calls DSTR. Split in two
;   rather than drawing directly because the F8 keeps exactly one return
;   address, so a routine reached by PI cannot itself PI into DSTR.
;
;   Takes its byte in r0, NOT in the accumulator: PI and JMP both load the
;   target address's high byte into A as scratch storage, so A DOES NOT SURVIVE
;   A CALL on this CPU. Nothing here may take or return an argument in A across
;   one. (Returning a value in A is fine -- POP does not touch it.)
;   Clobbers A, DC0.
; ---------------------------------------------------------------------------
DHEXS:	LR A,0
	SR 4
	AI 030H			; '0'
	CI 039H
	BP DHEXS1		; $39 >= A: it is a digit
	AI 007H			; otherwise skip ':' .. '@' to reach 'A'
DHEXS1:	ST
	LR A,0
	NI 0FH
	AI 030H
	CI 039H
	BP DHEXS2
	AI 007H
DHEXS2:	ST
	CLR
	ST
	POP
