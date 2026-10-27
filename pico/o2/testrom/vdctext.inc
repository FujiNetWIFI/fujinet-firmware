; vdctext.inc -- direct VDC character output, no BIOS.
;
; The character pointer is a byte offset into the 512-byte character generator
; (7-row glyphs in 8-byte slots), and the VDC adds (Y>>1) while drawing, so for
; glyph N at row Y the value is N*8 - (Y>>1), bit 8 in bit 0 of the control
; byte. Matches o2em's vdc.c (`c = chr + ypos; if (col&1) c += 256`).

CHRPTR  function idx,y,(idx*8-(y/2))&0FFh          ; low byte -> char reg byte 2
CHRCTL  function idx,y,col,col|((idx*8-(y/2))>>8&1) ; color + 9th pointer bit

; P1 bits (all active low unless noted)
P1_BANK0    equ 001h    ; P10 cart bank select, low bit
P1_BANK1    equ 002h    ; P11 cart bank select, high bit
P1_KBD      equ 004h    ; P12 0 = keyboard scanner enabled
P1_VDC      equ 008h    ; P13 0 = VDC on the bus
P1_XRAM     equ 010h    ; P14 0 = external RAM on the bus
P1_NC       equ 020h    ; P15 not connected
P1_COPY     equ 040h    ; P16 1 = RAM->VDC copy mode (also gates cart ~WR off)
P1_LUM      equ 080h    ; P17 1 = VDC luminance to the video mixer

; VDC selected: P13 low, the rest parked. P16 must stay low or the cartridge
; write strobe is disabled.
P1_SEL_VDC  equ P1_BANK0|P1_BANK1|P1_KBD|P1_XRAM|P1_NC|P1_LUM

; Joysticks and keyboard: P13, P14 high and P12 low. With the VDC still
; selected the read returns 0, every direction pressed at once.
P1_SEL_JOY  equ P1_BANK0|P1_BANK1|P1_VDC|P1_XRAM|P1_NC|P1_LUM
