; ---------------------------------------------------------------------------
; input.inc -- hand controllers and console buttons.
;
; Both are read through the same ports the video uses, and the read is gated by
; the LATCH the last write left behind: MAME's port_read_with_latch is
; (~ext | latch), so a bit only reads back as 0 when the input is pressed AND
; the corresponding latch bit is clear. So every scan writes 0 to the port
; first, reads, and complements -- after which a 1 bit means pressed.
;
; Port 0 bit 6 additionally gates the hand controllers: it must be CLEAR for
; ports 1 and 4 to return them at all.
;
; Console buttons on port 0: TIME $01, HOLD $02, MODE $04, START $08.
; Hand controller: right $01, left $02, down $04, up $08, twist CCW $10,
; twist CW $20, pull up $40, push down $80.
;
; "Push down" is the plunger on top of the stick -- the natural fire button on
; this console, and what this port uses for select.
; ---------------------------------------------------------------------------

; --- input state, in the arena ---
VIPCTL	EQU 08010H		; controller bits seen last scan
VIPPAN	EQU 08011H		; console buttons seen last scan

EV_NONE	EQU 0
EV_UP	EQU 1
EV_DOWN	EQU 2
EV_SEL	EQU 3
EV_BACK	EQU 4
EV_PGUP	EQU 5
EV_PGDN	EQU 6

; ---------------------------------------------------------------------------
; INSCAN -- poll everything and return ONE edge-triggered event in r0.
;   Edge-triggered: a held direction fires once. There is no auto-repeat, and
;   with nine rows on screen nothing needs one.
;   Clobbers A, r0, r1, r2, r3, DC0.
; ---------------------------------------------------------------------------
INSCAN:	CLR
	OUTS 0			; latch 0 clear: buttons live, controllers enabled
	CLR
	OUTS 1			; latch 1 clear: right controller readable
	INS 1
	COM
	LR 1,A			; r1 = controller, 1 = pressed
	INS 0
	COM
	NI 00FH
	LR 2,A			; r2 = console buttons, 1 = pressed

	; controller edges: pressed now AND not pressed last time
	DCI VIPCTL
	LM
	COM
	NS 1
	LR 0,A			; r0 = newly pressed controller bits
	DCI VIPCTL
	LR A,1
	ST

	; console-button edges. The current state has to be stashed before r2 is
	; overwritten with the edges, or the next scan compares against the wrong
	; thing and every press fires twice.
	DCI VIPPAN
	LM
	COM
	NS 2
	LR 3,A			; r3 = newly pressed buttons
	DCI VIPPAN
	LR A,2
	ST			; save the state we actually saw
	LR A,3
	LR 2,A

	; decode, in priority order
	LR A,0
	NI 008H			; stick up
	BNZ INUP
	LR A,0
	NI 004H			; stick down
	BNZ INDN
	LR A,0
	NI 080H			; plunger: select
	BNZ INSEL
	LR A,2
	NI 008H			; START: select
	BNZ INSEL
	LR A,2
	NI 004H			; MODE: back
	BNZ INBK
	LR A,0
	NI 010H			; twist counter-clockwise: page up
	BNZ INPU
	LR A,2
	NI 001H			; TIME: page up
	BNZ INPU
	LR A,0
	NI 020H			; twist clockwise: page down
	BNZ INPD
	LR A,2
	NI 002H			; HOLD: page down
	BNZ INPD
	CLR
	LR 0,A
	POP
INUP:	LI EV_UP
	LR 0,A
	POP
INDN:	LI EV_DOWN
	LR 0,A
	POP
INSEL:	LI EV_SEL
	LR 0,A
	POP
INBK:	LI EV_BACK
	LR 0,A
	POP
INPU:	LI EV_PGUP
	LR 0,A
	POP
INPD:	LI EV_PGDN
	LR 0,A
	POP
