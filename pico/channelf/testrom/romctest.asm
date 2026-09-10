; ---------------------------------------------------------------------------
; romctest.asm -- drive the F8 bus through every ROMC state a Channel F can
; actually produce, so emu/f8trace.py captures a trace that covers them.
;
; A normal program only reaches about 19 of the 32 states; in particular it
; never emits ROMC 05, the memory WRITE cycle that the cart-RAM design rests
; on. This exists purely to widen test_romc.c's coverage.
;
; Out of reach here and deliberately so:
;   0F/10/13  interrupt acknowledge -- needs a device asserting /INTREQ, and
;             the console wires no interrupt source
;   1E/1F     PC0 low/high onto the bus -- MAME's F8 core emits neither, so
;             no instruction can reach them
; ---------------------------------------------------------------------------
	CPU F3850

	ORG 0800H
	DB 55H
	DB 00H

ENTRY:	DI

LOOP:	; --- ROMC 05: ST, the store-through-DC0 write cycle ---
	DCI 0C000H
	LI 0AAH
	ST
	ST
	LI 55H
	ST

	; --- ROMC 02: LM, the read-through-DC0 cycle ---
	DCI 0900H
	LM
	LM

	; --- ROMC 0A: DC0 += signed. Both directions on purpose: a trace with
	;     only positive offsets cannot tell a sign-extending observer from
	;     one that widens the byte unsigned. ---
	DCI 0C000H
	LI 7FH
	ADC			; +127
	LI 80H
	ADC			; -128
	LI 0FFH
	ADC			; -1
	LIS 1
	ADC			; +1
	LI 0C0H
	ADC			; -64

	; --- ROMC 06/09: DC0 out to the H and Q register pairs ---
	DCI 01234H
	LR H,DC
	LR Q,DC

	; --- ROMC 16/19: DC0 loaded back from H and from Q ---
	LR DC,H
	LR DC,Q

	; --- ROMC 15/18: PC1 saved to K and loaded back ---
	LR K,P
	LR P,K

	; --- ROMC 1D: DC0/DC1 exchange ---
	DCI 02000H
	XDC
	DCI 03000H
	XDC

	; --- ROMC 1A/1B: I/O write and read ---
	LI 00H
	OUTS 0
	INS 0

	; --- ROMC 04/0D: PI and POP ---
	PI SUB

	; --- ROMC 17: PC0 loaded from Q, an indirect jump ---
	DCI TARGET
	LR Q,DC
	LR P0,Q

	DB 0FFH			; never reached

TARGET:	BR LOOP

SUB:	LIS 1
	POP

	END
