; ---------------------------------------------------------------------------
; fujidisp.inc -- Channel F display primitives.
;
; VRAM is 128x64 at 2bpp, WRITE-ONLY, reachable only through I/O ports:
;   port 1  bits 7-6  pixel value, inverted on the wire
;   port 4  bits 6-0  column, inverted
;   port 5  bits 5-0  row, inverted; bits 7-6 are SOUND and must be preserved
;   port 0  bit 5     write strobe, pulsed $60 -> $50, then a settling delay
;
; The port sequence is transcribed from the BIOS plotter ($0718-$0765),
; including the port 5 read-modify-write and the 4-iteration settle.
;
; Visible area is ~102x58 from (4,4); the safe area on a real set is ~95x58.
; A 4x6 cell over that gives 23 columns x 9 rows.
;
; Color constants follow ves.h: value in bits 7-6, complemented to port 1, so
; port value = ~r3 and VRAM pixel = r3>>6.
; ---------------------------------------------------------------------------

; Named by value, not color: the color depends on the row's palette.
CVAL0	EQU 000H
CVAL1	EQU 040H
CVAL2	EQU 080H
CVAL3	EQU 0C0H

; Palette is selected per row by the pixel values in columns 125 and 126:
;   index = ((col126 & 2) | (col125 >> 1)),  colors = colormap[index*4 + value]
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

BCLRSCR	EQU 000D0H		; BIOS clear screen, color in r3 bits 7-6

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
; Must run after a screen clear, which paints columns 125/126 too.
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
;   r1 = column, r2 = row, r3 = color.   Clobbers A, r8.
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
;   DC0 -> string, r1 = left pixel column, r2 = top pixel row, r3 = color.
; Advances 4 pixels per cell. Clobbers A, r0, r4-r8, DC0, DC1.
;
; Flat: one return address (PC1), so nesting would need the K-stack in the
; inner loop. Anything outside $20-$7F becomes '?'.
; ---------------------------------------------------------------------------
DSTR:	LR A,3
	COM
	OUTS 1			; color is constant for the whole string

DSTRLP:	LM			; A = char, DC0++
	CI 0
	BZ DSTRZ

	CI 1FH
	BP DSBAD		; A <= $1F
	CI FNTLST
	BM DSBAD		; A > $7F
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
	LI FNTCNT-1
	ADC			; same glyph, next row table (LM already stepped one)

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
;   The caller then calls DSTR on it; a PI'd routine cannot itself PI.
;
;   Takes r0, not A: PI and JMP load the target's high byte into A, so A does
;   not survive a call. Returning in A is fine; POP leaves it alone.
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

; ---------------------------------------------------------------------------
; DCLRR -- clear a band of cells to the background value.
;   r0 = cells wide, r1 = left pixel column, r2 = top pixel row.
;   Clears DCELLH pixel rows.
;
;   DSTR only sets pixels and VRAM cannot be read back.
;   Clobbers A, r0, r2 (it walks down the rows), r4, r5, r6, r7, r8.
;   Anything a caller needs across a DCLRR must live somewhere else -- this
;   eats most of the directly-addressable file.
; ---------------------------------------------------------------------------
DCLRR:	LI CVAL0
	COM
	OUTS 1			; background, constant for the whole fill
	LR A,0
	SL 1
	SL 1			; cells * DCELLW = pixels wide
	LR 4,A
	LIS DCELLH
	LR 7,A
DCLR1:	LR A,2
	COM
	NI 3FH
	LR 8,A
	INS 5
	NI 0C0H
	AS 8
	OUTS 5			; VRAM row, sound bits preserved
	LR A,1
	LR 5,A			; running column
	LR A,4
	LR 6,A			; pixels left in this row
DCLR2:	LR A,5
	COM
	OUTS 4
	LI 60H
	OUTS 0
	LI 50H
	OUTS 0
	LIS 4
	LR 0,A
DCLR3:	DS 0
	BNZ DCLR3
	LR A,5
	INC
	LR 5,A
	DS 6
	BNZ DCLR2
	LR A,2
	INC
	LR 2,A
	DS 7
	BNZ DCLR1
	POP
