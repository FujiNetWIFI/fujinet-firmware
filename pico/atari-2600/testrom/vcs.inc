; vcs.inc -- TIA and RIOT equates for Macroassembler AS.
;
; The 2600 world's standard vcs.h is a dasm companion file and is explicitly
; "please do not redistribute", so this is written from the hardware register
; map rather than copied. AS is the assembler the rest of this family already
; uses -- the o2 (8048), arcadia (2650) and channelf (F8) ports all build with
; it and CI already caches its build -- so the 2600 client joins them rather
; than adding dasm as a seventh toolchain.
;
; The 6507 has thirteen address lines, so everything mirrors. These are the
; canonical low addresses; TIA writes are $00-$2C, TIA reads $00-$0D, and the
; RIOT sits at $0280 with its RAM at $0080.

; ---------------- TIA, write ----------------
VSYNC   EQU     $00             ; bit 1: vertical sync
VBLANK  EQU     $01             ; bit 1: blank, bit 6/7: input latch/dump
WSYNC   EQU     $02             ; halt the CPU until the next scanline
RSYNC   EQU     $03
NUSIZ0  EQU     $04             ; player 0 copies / missile size
NUSIZ1  EQU     $05
COLUP0  EQU     $06
COLUP1  EQU     $07
COLUPF  EQU     $08
COLUBK  EQU     $09
CTRLPF  EQU     $0A
REFP0   EQU     $0B
REFP1   EQU     $0C
PF0     EQU     $0D
PF1     EQU     $0E
PF2     EQU     $0F
RESP0   EQU     $10             ; strobe: reset player 0 to the beam
RESP1   EQU     $11
RESM0   EQU     $12
RESM1   EQU     $13
RESBL   EQU     $14
AUDC0   EQU     $15
AUDC1   EQU     $16
AUDF0   EQU     $17
AUDF1   EQU     $18
AUDV0   EQU     $19
AUDV1   EQU     $1A
GRP0    EQU     $1B             ; player 0 graphics
GRP1    EQU     $1C
ENAM0   EQU     $1D
ENAM1   EQU     $1E
ENABL   EQU     $1F
HMP0    EQU     $20             ; fine motion, high nibble, +7..-8
HMP1    EQU     $21
HMM0    EQU     $22
HMM1    EQU     $23
HMBL    EQU     $24
VDELP0  EQU     $25             ; bit 0: delay GRP0 by one write
VDELP1  EQU     $26
VDELBL  EQU     $27
RESMP0  EQU     $28
RESMP1  EQU     $29
HMOVE   EQU     $2A             ; strobe: apply the HM registers
HMCLR   EQU     $2B
CXCLR   EQU     $2C

; ---------------- TIA, read ----------------
CXM0P   EQU     $00
CXM1P   EQU     $01
CXP0FB  EQU     $02
CXP1FB  EQU     $03
CXM0FB  EQU     $04
CXM1FB  EQU     $05
CXBLPF  EQU     $06
CXPPMM  EQU     $07
INPT0   EQU     $08
INPT1   EQU     $09
INPT2   EQU     $0A
INPT3   EQU     $0B
INPT4   EQU     $0C             ; bit 7 low: joystick 0 fire
INPT5   EQU     $0D

; ---------------- RIOT ----------------
SWCHA   EQU     $0280           ; joysticks: P0 in bits 7-4 (R,L,D,U low)
SWACNT  EQU     $0281
SWCHB   EQU     $0282           ; bit 0 RESET, bit 1 SELECT (low = pressed)
SWBCNT  EQU     $0283
INTIM   EQU     $0284
TIM1T   EQU     $0294
TIM8T   EQU     $0295
TIM64T  EQU     $0296
T1024T  EQU     $0297

; Joystick 0 direction bits in SWCHA, active low.
J0RIGHT EQU     $80
J0LEFT  EQU     $40
J0DOWN  EQU     $20
J0UP    EQU     $10
