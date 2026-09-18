; vdctext.inc -- direct VDC character output, no BIOS.
;
; The VDC's character-pointer byte is a BYTE OFFSET into the 512-byte character
; generator, and the VDC adds (Y>>1) to it while drawing. Glyphs are 7 rows in
; an 8-byte slot, so for glyph index N at row Y the register value is
;   N*8 - (Y>>1)
; with bit 8 of that going into bit 0 of the char's control byte. Verified
; against o2em's cset.c and vdc.c:536 (`c = chr + ypos; if (col&1) c += 256`).

CHRPTR  function idx,y,(idx*8-(y/2))&0FFh          ; low byte -> char reg byte 2
CHRCTL  function idx,y,col,col|((idx*8-(y/2))>>8&1) ; colour + 9th pointer bit

; P1 bits (all active low unless noted)
P1_BANK0    equ 001h    ; P10 cart bank select, low bit
P1_BANK1    equ 002h    ; P11 cart bank select, high bit
P1_KBD      equ 004h    ; P12 0 = keyboard scanner enabled
P1_VDC      equ 008h    ; P13 0 = VDC on the bus
P1_XRAM     equ 010h    ; P14 0 = external RAM on the bus
P1_NC       equ 020h    ; P15 not connected
P1_COPY     equ 040h    ; P16 1 = RAM->VDC copy mode (also gates cart ~WR off)
P1_LUM      equ 080h    ; P17 1 = VDC luminance to the video mixer

; VDC selected: P13 low, everything else parked. Copy mode MUST stay low or the
; cartridge write strobe is disabled.
P1_SEL_VDC  equ P1_BANK0|P1_BANK1|P1_KBD|P1_XRAM|P1_NC|P1_LUM

; Joysticks and keyboard: VDC and external RAM both OFF the bus (P13, P14 high)
; and the scanner enabled (P12 low). Getting this wrong is not a quiet failure --
; with the VDC still selected the bus read never reaches the joystick path at
; all and returns 0, which is every direction pressed at once.
P1_SEL_JOY  equ P1_BANK0|P1_BANK1|P1_VDC|P1_XRAM|P1_NC|P1_LUM
