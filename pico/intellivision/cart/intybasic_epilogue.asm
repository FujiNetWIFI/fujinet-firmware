    ;
    ; Epilogue for IntyBASIC programs
    ; by Oscar Toledo G.  http://nanochess.org/
    ;
    ; Revision: Jan/30/2014. Moved GRAM code below MOB updates.
    ;                        Added comments.
    ; Revision: Feb/26/2014. Optimized access to collision registers
    ;                        per DZ-Jay suggestion. Added scrolling
    ;                        routines with optimization per intvnut
    ;                        suggestion. Added border/mask support.
    ; Revision: Apr/02/2014. Added support to set MODE (color stack
    ;                        or foreground/background), added support
    ;                        for SCREEN statement.
    ; Revision: Aug/19/2014. Solved bug in bottom scroll, moved an
    ;                        extra unneeded line.
    ; Revision: Aug/26/2014. Integrated music player and NTSC/PAL
    ;                        detection.
    ; Revision: Oct/24/2014. Adjust in some comments.
    ; Revision: Nov/13/2014. Integrated Joseph Zbiciak's routines
    ;                        for printing numbers.
    ; Revision: Nov/17/2014. Redesigned MODE support to use a single
    ;                        variable.
    ; Revision: Nov/21/2014. Added Intellivoice support routines made
    ;                        by Joseph Zbiciak.
    ; Revision: Dec/11/2014. Optimized keypad decode routines.
    ; Revision: Jan/25/2015. Added marker for insertion of ON FRAME GOSUB
    ; Revision: Feb/17/2015. Allows to deactivate music player (PLAY NONE)
    ; Revision: Apr/21/2015. Accelerates common case of keypad not pressed.
    ;                        Added ECS ROM disable code.
    ; Revision: Apr/22/2015. Added Joseph Zbiciak accelerated multiplication
    ;                        routines.
    ; Revision: Jun/04/2015. Optimized play_music (per GroovyBee suggestion)
    ; Revision: Jul/25/2015. Added infinite loop at start to avoid crashing
    ;                        with empty programs. Solved bug where _color
    ;                        didn't started with white.
    ; Revision: Aug/20/2015. Moved ECS mapper disable code so nothing gets
    ;                        after it (GroovyBee 42K sample code)
    ; Revision: Aug/21/2015. Added Joseph Zbiciak routines for JLP Flash
    ;                        handling.
    ; Revision: Aug/31/2015. Added CPYBLK2 for SCREEN fifth argument.
    ; Revision: Sep/01/2015. Defined labels Q1 and Q2 as alias.
    ; Revision: Jan/22/2016. Music player allows not to use noise channel
    ;                        for drums. Allows setting music volume.
    ; Revision: Jan/23/2016. Added jump inside of music (for MUSIC JUMP)
    ; Revision: May/03/2016. Preserves current mode in bit 0 of _mode_select
    ; Revision: Oct/21/2016. Added C7 in notes table, it was missing. (thanks
    ;                        mmarrero)
    ; Revision: Jan/09/2018. Initializes scroll offset registers (useful when
    ;                        starting from $4800). Uses slightly less space.
    ; Revision: Feb/05/2018. Added IV_HUSH.
    ; Revision: Mar/01/2018. Added support for music tracker over ECS.
    ; Revision: Sep/25/2018. Solved bug in mixer for ECS drums.
    ; Revision: Oct/30/2018. Small optimization in music player.
    ; Revision: Jan/09/2019. Solved bug where it would play always like
    ;                        PLAY SIMPLE NO DRUMS.
    ; Revision: May/18/2019. Solved bug where drums failed in ECS side.
    ;

    ;
    ; Avoids empty programs to crash
    ;
stuck:    B stuck

    ;
    ; Copy screen helper for SCREEN wide statement
    ;

CPYBLK2:    PROC
    MOVR R0,R3        ; Offset
    MOVR R5,R2
    PULR R0
    PULR R1
    PULR R5
    PULR R4
    PSHR R2
    SUBR R1,R3

@@1:    PSHR R3
    MOVR R1,R3        ; Init line copy
@@2:    MVI@ R4,R2        ; Copy line
    MVO@ R2,R5
    DECR R3
    BNE @@2
    PULR R3         ; Add offset to start in next line
    ADDR R3,R4
    SUBR R1,R5
    ADDI #20,R5
    DECR R0         ; Count lines
    BNE @@1

    RETURN
    ENDP

    ;
    ; Copy screen helper for SCREEN statement
    ;
CPYBLK:    PROC
    BEGIN
    MOVR R3,R4
    MOVR R2,R5

@@1:    MOVR R1,R3          ; Init line copy
@@2:    MVI@ R4,R2          ; Copy line
    MVO@ R2,R5
    DECR R3
    BNE @@2
    MVII #20,R3         ; Add offset to start in next line
    SUBR R1,R3
    ADDR R3,R4
    ADDR R3,R5
    DECR R0         ; Count lines
    BNE @@1
    RETURN
    ENDP

    ;
    ; Wait for interruption
    ;
_wait:  PROC

    IF DEFINED intybasic_keypad
    MVI $01FF,R0
    COMR R0
    ANDI #$FF,R0
    CMP _cnt1_p0,R0
    BNE @@2
    CMP _cnt1_p1,R0
    BNE @@2
    TSTR R0        ; Accelerates common case of key not pressed
    MVII #_keypad_table+13,R4
    BEQ @@4
    MVII #_keypad_table,R4
    REPEAT 6
    CMP@ R4,R0
    BEQ @@4
    CMP@ R4,R0
    BEQ @@4
    ENDR
    INCR R4
@@4:    SUBI #_keypad_table+1,R4
    MVO R4,_cnt1_key

@@2:    MVI _cnt1_p1,R1
    MVO R1,_cnt1_p0
    MVO R0,_cnt1_p1

    MVI $01FE,R0
    COMR R0
    ANDI #$FF,R0
    CMP _cnt2_p0,R0
    BNE @@5
    CMP _cnt2_p1,R0
    BNE @@5
    TSTR R0        ; Accelerates common case of key not pressed
    MVII #_keypad_table+13,R4
    BEQ @@7
    MVII #_keypad_table,R4
    REPEAT 6
    CMP@ R4,R0
    BEQ @@7
    CMP@ R4,R0
    BEQ @@7
    ENDR

    INCR R4
@@7:    SUBI #_keypad_table+1,R4
    MVO R4,_cnt2_key

@@5:    MVI _cnt2_p1,R1
    MVO R1,_cnt2_p0
    MVO R0,_cnt2_p1
    ENDI

    CLRR    R0
    MVO     R0,_int     ; Clears waiting flag
@@1:    CMP     _int,  R0       ; Waits for change
    BEQ     @@1
    JR      R5          ; Returns
    ENDP

    ;
    ; Keypad table
    ;
_keypad_table:      PROC
    DECLE $48,$81,$41,$21,$82,$42,$22,$84,$44,$24,$88,$28
    ENDP

_set_isr:    PROC
    MVI@ R5,R0
    MVO R0,ISRVEC
    SWAP R0
    MVO R0,ISRVEC+1
    JR R5
    ENDP

    ;
    ; Interruption routine
    ;
_int_vector:     PROC

    IF DEFINED intybasic_stack
    CMPI #$308,R6
    BNC @@vs
    MVO R0,$20    ; Enables display
    MVI $21,R0    ; Activates Color Stack mode
    CLRR R0
    MVO R0,$28
    MVO R0,$29
    MVO R0,$2A
    MVO R0,$2B
    MVII #@@vs1,R4
    MVII #$200,R5
    MVII #20,R1
@@vs2:    MVI@ R4,R0
    MVO@ R0,R5
    DECR R1
    BNE @@vs2
    RETURN

    ; Stack Overflow message
@@vs1:    DECLE 0,0,0,$33*8+7,$54*8+7,$41*8+7,$43*8+7,$4B*8+7,$00*8+7
    DECLE $4F*8+7,$56*8+7,$45*8+7,$52*8+7,$46*8+7,$4C*8+7
    DECLE $4F*8+7,$57*8+7,0,0,0

@@vs:
    ENDI

    MVII #1,R1
    MVO R1,_int    ; Indicates interrupt happened.

    MVI _mode_select,R0
    SARC R0,2
    BNE @@ds
    MVO R0,$20    ; Enables display
@@ds:    BNC @@vi14
    MVO R0,$21    ; Foreground/background mode
    BNOV @@vi0
    B @@vi15

@@vi14:    MVI $21,R0    ; Color stack mode
    BNOV @@vi0
    CLRR R1
    MVI _color,R0
    MVO R0,$28
    SWAP R0
    MVO R0,$29
    SLR R0,2
    SLR R0,2
    MVO R0,$2A
    SWAP R0
    MVO R0,$2B
@@vi15:
    MVO R1,_mode_select
    MVII #7,R0
    MVO R0,_color       ; Default color for PRINT "string"
@@vi0:

    BEGIN

    MVI _border_color,R0
    MVO     R0,     $2C     ; Border color
    MVI _border_mask,R0
    MVO     R0,     $32     ; Border mask
    ;
    ; Save collision registers for further use and clear them
    ;
    MVII #$18,R4
    MVII #_col0,R5
    MVI@ R4,R0
    MVO@ R0,R5  ; _col0
    MVI@ R4,R0
    MVO@ R0,R5  ; _col1
    MVI@ R4,R0
    MVO@ R0,R5  ; _col2
    MVI@ R4,R0
    MVO@ R0,R5  ; _col3
    MVI@ R4,R0
    MVO@ R0,R5  ; _col4
    MVI@ R4,R0
    MVO@ R0,R5  ; _col5
    MVI@ R4,R0
    MVO@ R0,R5  ; _col6
    MVI@ R4,R0
    MVO@ R0,R5  ; _col7

    IF DEFINED intybasic_scroll

    ;
    ; Scrolling things
    ;
    MVI _scroll_x,R0
    MVO R0,$30
    MVI _scroll_y,R0
    MVO R0,$31
    ENDI

    ;
    ; Updates sprites (MOBs)
    ;
    MOVR R5,R4    ; MVII #_mobs,R4
    CLRR R5        ; X-coordinates
    REPEAT 8
    MVI@ R4,R0
    MVO@ R0,R5
    MVI@ R4,R0
    MVO@ R0,R5
    MVI@ R4,R0
    MVO@ R0,R5
    ENDR
    CLRR R0        ; Erase collision bits (R5 = $18)
    MVO@ R0,R5
    MVO@ R0,R5
    MVO@ R0,R5
    MVO@ R0,R5
    MVO@ R0,R5
    MVO@ R0,R5
    MVO@ R0,R5
    MVO@ R0,R5

    IF DEFINED intybasic_music
         MVI _ntsc,R0
    RRC R0,1     ; PAL?
    BNC @@vo97      ; Yes, always emit sound
    MVI _music_frame,R0
    INCR R0
    CMPI #6,R0
    BNE @@vo14
    CLRR R0
@@vo14:    MVO R0,_music_frame
    BEQ @@vo15
@@vo97:    CALL _emit_sound
    IF DEFINED intybasic_music_ecs
    CALL _emit_sound_ecs
    ENDI
@@vo15:
    ENDI

    ;
    ; Detect GRAM definition
    ;
    MVI _gram_bitmap,R4
    TSTR R4
    BEQ @@vi1
    MVI _gram_target,R1
    SLL R1,2
    SLL R1,1
    ADDI #$3800,R1
    MOVR R1,R5
    MVI _gram_total,R0
@@vi3:
    MVI@    R4,     R1
    MVO@    R1,     R5
    SWAP    R1
    MVO@    R1,     R5
    MVI@    R4,     R1
    MVO@    R1,     R5
    SWAP    R1
    MVO@    R1,     R5
    MVI@    R4,     R1
    MVO@    R1,     R5
    SWAP    R1
    MVO@    R1,     R5
    MVI@    R4,     R1
    MVO@    R1,     R5
    SWAP    R1
    MVO@    R1,     R5
    DECR R0
    BNE @@vi3
    MVO R0,_gram_bitmap
@@vi1:
    MVI _gram2_bitmap,R4
    TSTR R4
    BEQ @@vii1
    MVI _gram2_target,R1
    SLL R1,2
    SLL R1,1
    ADDI #$3800,R1
    MOVR R1,R5
    MVI _gram2_total,R0
@@vii3:
    MVI@    R4,     R1
    MVO@    R1,     R5
    SWAP    R1
    MVO@    R1,     R5
    MVI@    R4,     R1
    MVO@    R1,     R5
    SWAP    R1
    MVO@    R1,     R5
    MVI@    R4,     R1
    MVO@    R1,     R5
    SWAP    R1
    MVO@    R1,     R5
    MVI@    R4,     R1
    MVO@    R1,     R5
    SWAP    R1
    MVO@    R1,     R5
    DECR R0
    BNE @@vii3
    MVO R0,_gram2_bitmap
@@vii1:

    IF DEFINED intybasic_scroll
    ;
    ; Frame scroll support
    ;
    MVI _scroll_d,R0
    TSTR R0
    BEQ @@vi4
    CLRR R1
    MVO R1,_scroll_d
    DECR R0     ; Left
    BEQ @@vi5
    DECR R0     ; Right
    BEQ @@vi6
    DECR R0     ; Top
    BEQ @@vi7
    DECR R0     ; Bottom
    BEQ @@vi8
    B @@vi4

@@vi5:  MVII #$0200,R4
    MOVR R4,R5
    INCR R5
    MVII #12,R1
@@vi12: MVI@ R4,R2
    MVI@ R4,R3
    REPEAT 8
    MVO@ R2,R5
    MVI@ R4,R2
    MVO@ R3,R5
    MVI@ R4,R3
    ENDR
    MVO@ R2,R5
    MVI@ R4,R2
    MVO@ R3,R5
    MVO@ R2,R5
    INCR R4
    INCR R5
    DECR R1
    BNE @@vi12
    B @@vi4

@@vi6:  MVII #$0201,R4
    MVII #$0200,R5
    MVII #12,R1
@@vi11:
    REPEAT 19
    MVI@ R4,R0
    MVO@ R0,R5
    ENDR
    INCR R4
    INCR R5
    DECR R1
    BNE @@vi11
    B @@vi4

    ;
    ; Complex routine to be ahead of STIC display
    ; Moves first the top 6 lines, saves intermediate line
    ; Then moves the bottom 6 lines and restores intermediate line
    ;
@@vi7:  MVII #$0264,R4
    MVII #5,R1
    MVII #_scroll_buffer,R5
    REPEAT 20
    MVI@ R4,R0
    MVO@ R0,R5
    ENDR
    SUBI #40,R4
    MOVR R4,R5
    ADDI #20,R5
@@vi10:
    REPEAT 20
    MVI@ R4,R0
    MVO@ R0,R5
    ENDR
    SUBI #40,R4
    SUBI #40,R5
    DECR R1
    BNE @@vi10
    MVII #$02C8,R4
    MVII #$02DC,R5
    MVII #5,R1
@@vi13:
    REPEAT 20
    MVI@ R4,R0
    MVO@ R0,R5
    ENDR
    SUBI #40,R4
    SUBI #40,R5
    DECR R1
    BNE @@vi13
    MVII #_scroll_buffer,R4
    REPEAT 20
    MVI@ R4,R0
    MVO@ R0,R5
    ENDR
    B @@vi4

@@vi8:  MVII #$0214,R4
    MVII #$0200,R5
    MVII #$DC/4,R1
@@vi9:
    REPEAT 4
    MVI@ R4,R0
    MVO@ R0,R5
    ENDR
    DECR R1
    BNE @@vi9
    B @@vi4

@@vi4:
    ENDI

    IF DEFINED intybasic_voice
    ;
    ; Intellivoice support
    ;
    CALL IV_ISR
    ENDI

    ;
    ; Random number generator
    ;
    CALL _next_random

    IF DEFINED intybasic_music
    ; Generate sound for next frame
           MVI _ntsc,R0
    RRC R0,1     ; PAL?
    BNC @@vo98      ; Yes, always generate sound
    MVI _music_frame,R0
    TSTR R0
    BEQ @@vo16
@@vo98: CALL _generate_music
@@vo16:
    ENDI

    ; Increase frame number
    MVI _frame,R0
    INCR R0
    MVO R0,_frame

    ; This mark is for ON FRAME GOSUB support
    ;IntyBASIC MARK DON'T CHANGE

    RETURN
    ENDP

    ;
    ; Generates the next random number
    ;
_next_random:    PROC

MACRO _ROR
    RRC R0,1
    MOVR R0,R2
    SLR R2,2
    SLR R2,2
    ANDI #$0800,R2
    SLR R2,2
    SLR R2,2
    ANDI #$007F,R0
    XORR R2,R0
ENDM
    MVI _rand,R0
    SETC
    _ROR
    XOR _frame,R0
    _ROR
    XOR _rand,R0
    _ROR
    XORI #9,R0
    MVO R0,_rand
    JR R5
    ENDP

    IF DEFINED intybasic_music

    ;
    ; Music player, comes from my game Princess Quest for Intellivision
    ; so it's a practical tracker used in a real game ;) and with enough
    ; features.
    ;

    ; NTSC frequency for notes (based on 3.579545 mhz)
ntsc_note_table:    PROC
    ; Silence - 0
    DECLE 0
    ; Octave 2 - 1
    DECLE 1721,1621,1532,1434,1364,1286,1216,1141,1076,1017,956,909
    ; Octave 3 - 13
    DECLE 854,805,761,717,678,639,605,571,538,508,480,453
    ; Octave 4 - 25
    DECLE 427,404,380,360,339,321,302,285,270,254,240,226
    ; Octave 5 - 37
    DECLE 214,202,191,180,170,160,151,143,135,127,120,113
    ; Octave 6 - 49
    DECLE 107,101,95,90,85,80,76,71,67,64,60,57
    ; Octave 7 - 61
    DECLE 54
    ; Space for two notes more
    ENDP

    ; PAL frequency for notes (based on 4 mhz)
pal_note_table:    PROC
    ; Silence - 0
    DECLE 0
    ; Octava 2 - 1
    DECLE 1923,1812,1712,1603,1524,1437,1359,1276,1202,1136,1068,1016
    ; Octava 3 - 13
    DECLE 954,899,850,801,758,714,676,638,601,568,536,506
    ; Octava 4 - 25
    DECLE 477,451,425,402,379,358,338,319,301,284,268,253
    ; Octava 5 - 37
    DECLE 239,226,213,201,190,179,169,159,150,142,134,127
    ; Octava 6 - 49
    DECLE 120,113,106,100,95,89,84,80,75,71,67,63
    ; Octava 7 - 61
    DECLE 60
    ; Space for two notes more
    ENDP
    ENDI

    ;
    ; Music tracker init
    ;
_init_music:    PROC
    IF DEFINED intybasic_music
    MVI _ntsc,R0
    RRC R0,1
    MVII #ntsc_note_table,R0
    BC @@0
    MVII #pal_note_table,R0
@@0:    MVO R0,_music_table
    MVII #$38,R0    ; $B8 blocks controllers o.O!
    MVO R0,_music_mix
    IF DEFINED intybasic_music_ecs
    MVO R0,_music2_mix
    ENDI
    CLRR R0
    ELSE
    JR R5        ; Tracker disabled (no PLAY statement used)
    ENDI
    ENDP

    IF DEFINED intybasic_music
    ;
    ; Start music
    ; R0 = Pointer to music
    ;
_play_music:    PROC
    MVII #1,R1
    MOVR R1,R3
    MOVR R0,R2
    BEQ @@1
    MVI@ R2,R3
    INCR R2
@@1:    MVO R2,_music_p
    MVO R2,_music_start
    SWAP R2
    MVO R2,_music_start+1
    MVO R3,_music_t
    MVO R1,_music_tc
    JR R5

    ENDP

    ;
    ; Generate music
    ;
_generate_music:    PROC
    BEGIN
    MVI _music_mix,R0
    ANDI #$C0,R0
    XORI #$38,R0
    MVO R0,_music_mix
    IF DEFINED intybasic_music_ecs
    MVI _music2_mix,R0
    ANDI #$C0,R0
    XORI #$38,R0
    MVO R0,_music2_mix
    ENDI
    CLRR R1            ; Turn off volume for the three sound channels
    MVO R1,_music_vol1
    MVO R1,_music_vol2
    MVI _music_tc,R3
    MVO R1,_music_vol3
    IF DEFINED intybasic_music_ecs
    MVO R1,_music2_vol1
    NOP
    MVO R1,_music2_vol2
    MVO R1,_music2_vol3
    ENDI
    DECR R3
    MVO R3,_music_tc
    BNE @@6
    ; R3 is zero from here up to @@6
    MVI _music_p,R4
@@15:    TSTR R4        ; Silence?
    BEQ @@43    ; Keep quiet
@@41:    MVI@ R4,R0
    MVI@ R4,R1
    MVI _music_t,R2
    CMPI #$FA00,R1    ; Volume?
    BNC @@42
    IF DEFINED intybasic_music_volume
    BEQ @@40
    ENDI
    CMPI #$FF00,R1    ; Speed?
    BEQ @@39
    CMPI #$FB00,R1    ; Return?
    BEQ @@38
    CMPI #$FC00,R1    ; Gosub?
    BEQ @@37
    CMPI #$FE00,R1    ; The end?
    BEQ @@36       ; Keep quiet
;    CMPI #$FD00,R1    ; Repeat?
;    BNE @@42
    MVI _music_start+1,R0
    SWAP R0
    ADD _music_start,R0
    MOVR R0,R4
    B @@15

    IF DEFINED intybasic_music_volume
@@40:
    MVO R0,_music_vol
    B @@41
    ENDI

@@39:    MVO R0,_music_t
    MOVR R0,R2
    B @@41

@@38:    MVI _music_gosub,R4
    B @@15

@@37:    MVO R4,_music_gosub
@@36:    MOVR R0,R4    ; Jump, zero will make it quiet
    B @@15

@@43:    MVII #1,R0
    MVO R0,_music_tc
    B @@0

@@42:     MVO R2,_music_tc    ; Restart note time
         MVO R4,_music_p

    MOVR R0,R2
    ANDI #$FF,R2
    CMPI #$3F,R2    ; Sustain note?
    BEQ @@1
    MOVR R2,R4
    ANDI #$3F,R4
    MVO R4,_music_n1    ; Note
    MVO R3,_music_s1    ; Waveform
    ANDI #$C0,R2
    MVO R2,_music_i1    ; Instrument

@@1:    SWAP R0
    ANDI #$FF,R0
    CMPI #$3F,R0    ; Sustain note?
    BEQ @@2
    MOVR R0,R4
    ANDI #$3F,R4
    MVO R4,_music_n2    ; Note
    MVO R3,_music_s2    ; Waveform
    ANDI #$C0,R0
    MVO R0,_music_i2    ; Instrument

@@2:    MOVR R1,R2
    ANDI #$FF,R2
    CMPI #$3F,R2    ; Sustain note?
    BEQ @@3
    MOVR R2,R4
    ANDI #$3F,R4
    MVO R4,_music_n3    ; Note
    MVO R3,_music_s3    ; Waveform
    ANDI #$C0,R2
    MVO R2,_music_i3    ; Instrument

@@3:    SWAP R1
    MVO R1,_music_n4
    MVO R3,_music_s4

    IF DEFINED intybasic_music_ecs
    MVI _music_p,R4
    MVI@ R4,R0
    MVI@ R4,R1
    MVO R4,_music_p

    MOVR R0,R2
    ANDI #$FF,R2
    CMPI #$3F,R2    ; Sustain note?
    BEQ @@33
    MOVR R2,R4
    ANDI #$3F,R4
    MVO R4,_music_n5    ; Note
    MVO R3,_music_s5    ; Waveform
    ANDI #$C0,R2
    MVO R2,_music_i5    ; Instrument

@@33:    SWAP R0
    ANDI #$FF,R0
    CMPI #$3F,R0    ; Sustain note?
    BEQ @@34
    MOVR R0,R4
    ANDI #$3F,R4
    MVO R4,_music_n6    ; Note
    MVO R3,_music_s6    ; Waveform
    ANDI #$C0,R0
    MVO R0,_music_i6    ; Instrument

@@34:    MOVR R1,R2
    ANDI #$FF,R2
    CMPI #$3F,R2    ; Sustain note?
    BEQ @@35
    MOVR R2,R4
    ANDI #$3F,R4
    MVO R4,_music_n7    ; Note
    MVO R3,_music_s7    ; Waveform
    ANDI #$C0,R2
    MVO R2,_music_i7    ; Instrument

@@35:    MOVR R1,R2
    SWAP R2
    MVO R2,_music_n8
    MVO R3,_music_s8

    ENDI

    ;
    ; Construct main voice
    ;
@@6:    MVI _music_n1,R3    ; Read note
    TSTR R3        ; There is note?
    BEQ @@7        ; No, jump
    MVI _music_s1,R1
    MVI _music_i1,R2
    MOVR R1,R0
    CALL _note2freq
    MVO R3,_music_freq10    ; Note in voice A
    SWAP R3
    MVO R3,_music_freq11
    MVO R1,_music_vol1
    ; Increase time for instrument waveform
    INCR R0
    CMPI #$18,R0
    BNE @@20
    SUBI #$08,R0
@@20:    MVO R0,_music_s1

@@7:    MVI _music_n2,R3    ; Read note
    TSTR R3        ; There is note?
    BEQ @@8        ; No, jump
    MVI _music_s2,R1
    MVI _music_i2,R2
    MOVR R1,R0
    CALL _note2freq
    MVO R3,_music_freq20    ; Note in voice B
    SWAP R3
    MVO R3,_music_freq21
    MVO R1,_music_vol2
    ; Increase time for instrument waveform
    INCR R0
    CMPI #$18,R0
    BNE @@21
    SUBI #$08,R0
@@21:    MVO R0,_music_s2

@@8:    MVI _music_n3,R3    ; Read note
    TSTR R3        ; There is note?
    BEQ @@9        ; No, jump
    MVI _music_s3,R1
    MVI _music_i3,R2
    MOVR R1,R0
    CALL _note2freq
    MVO R3,_music_freq30    ; Note in voice C
    SWAP R3
    MVO R3,_music_freq31
    MVO R1,_music_vol3
    ; Increase time for instrument waveform
    INCR R0
    CMPI #$18,R0
    BNE @@22
    SUBI #$08,R0
@@22:    MVO R0,_music_s3

@@9:    MVI _music_n4,R0    ; Read drum
    DECR R0        ; There is drum?
    BMI @@4        ; No, jump
    MVI _music_s4,R1
                   ; 1 - Strong
    BNE @@5
    CMPI #3,R1
    BGE @@12
@@10:    MVII #5,R0
    MVO R0,_music_noise
    CALL _activate_drum
    B @@12

@@5:    DECR R0        ;2 - Short
    BNE @@11
    TSTR R1
    BNE @@12
    MVII #8,R0
    MVO R0,_music_noise
    CALL _activate_drum
    B @@12

@@11:    ;DECR R0    ; 3 - Rolling
    ;BNE @@12
    CMPI #2,R1
    BLT @@10
    MVI _music_t,R0
    SLR R0,1
    CMPR R0,R1
    BLT @@12
    ADDI #2,R0
    CMPR R0,R1
    BLT @@10
    ; Increase time for drum waveform
@@12:   INCR R1
    MVO R1,_music_s4

@@4:
    IF DEFINED intybasic_music_ecs
    ;
    ; Construct main voice
    ;
    MVI _music_n5,R3    ; Read note
    TSTR R3        ; There is note?
    BEQ @@23    ; No, jump
    MVI _music_s5,R1
    MVI _music_i5,R2
    MOVR R1,R0
    CALL _note2freq
    MVO R3,_music2_freq10    ; Note in voice A
    SWAP R3
    MVO R3,_music2_freq11
    MVO R1,_music2_vol1
    ; Increase time for instrument waveform
    INCR R0
    CMPI #$18,R0
    BNE @@24
    SUBI #$08,R0
@@24:    MVO R0,_music_s5

@@23:    MVI _music_n6,R3    ; Read note
    TSTR R3        ; There is note?
    BEQ @@25        ; No, jump
    MVI _music_s6,R1
    MVI _music_i6,R2
    MOVR R1,R0
    CALL _note2freq
    MVO R3,_music2_freq20    ; Note in voice B
    SWAP R3
    MVO R3,_music2_freq21
    MVO R1,_music2_vol2
    ; Increase time for instrument waveform
    INCR R0
    CMPI #$18,R0
    BNE @@26
    SUBI #$08,R0
@@26:    MVO R0,_music_s6

@@25:    MVI _music_n7,R3    ; Read note
    TSTR R3        ; There is note?
    BEQ @@27        ; No, jump
    MVI _music_s7,R1
    MVI _music_i7,R2
    MOVR R1,R0
    CALL _note2freq
    MVO R3,_music2_freq30    ; Note in voice C
    SWAP R3
    MVO R3,_music2_freq31
    MVO R1,_music2_vol3
    ; Increase time for instrument waveform
    INCR R0
    CMPI #$18,R0
    BNE @@28
    SUBI #$08,R0
@@28:    MVO R0,_music_s7

@@27:    MVI _music_n8,R0    ; Read drum
    DECR R0        ; There is drum?
    BMI @@0        ; No, jump
    MVI _music_s8,R1
                   ; 1 - Strong
    BNE @@29
    CMPI #3,R1
    BGE @@31
@@32:    MVII #5,R0
    MVO R0,_music2_noise
    CALL _activate_drum_ecs
    B @@31

@@29:    DECR R0        ;2 - Short
    BNE @@30
    TSTR R1
    BNE @@31
    MVII #8,R0
    MVO R0,_music2_noise
    CALL _activate_drum_ecs
    B @@31

@@30:    ;DECR R0    ; 3 - Rolling
    ;BNE @@31
    CMPI #2,R1
    BLT @@32
    MVI _music_t,R0
    SLR R0,1
    CMPR R0,R1
    BLT @@31
    ADDI #2,R0
    CMPR R0,R1
    BLT @@32
    ; Increase time for drum waveform
@@31:    INCR R1
    MVO R1,_music_s8

    ENDI
@@0:    RETURN
    ENDP

    ;
    ; Translates note number to frequency
    ; R3 = Note
    ; R1 = Position in waveform for instrument
    ; R2 = Instrument
    ;
_note2freq:    PROC
    ADD _music_table,R3
    MVI@ R3,R3
    SWAP R2
    BEQ _piano_instrument
    RLC R2,1
    BNC _clarinet_instrument
    BPL _flute_instrument
;    BMI _bass_instrument
    ENDP

    ;
    ; Generates a bass
    ;
_bass_instrument:    PROC
    SLL R3,2    ; Lower 2 octaves
    ADDI #_bass_volume,R1
    MVI@ R1,R1    ; Bass effect
    IF DEFINED intybasic_music_volume
    B _global_volume
    ELSE
    JR R5
    ENDI
    ENDP

_bass_volume:    PROC
    DECLE 12,13,14,14,13,12,12,12
    DECLE 11,11,12,12,11,11,12,12
    DECLE 11,11,12,12,11,11,12,12
    ENDP

    ;
    ; Generates a piano
    ; R3 = Frequency
    ; R1 = Waveform position
    ;
    ; Output:
    ; R3 = Frequency.
    ; R1 = Volume.
    ;
_piano_instrument:    PROC
    ADDI #_piano_volume,R1
    MVI@ R1,R1
    IF DEFINED intybasic_music_volume
    B _global_volume
    ELSE
    JR R5
    ENDI
    ENDP

_piano_volume:    PROC
    DECLE 14,13,13,12,12,11,11,10
    DECLE 10,9,9,8,8,7,7,6
    DECLE 6,6,7,7,6,6,5,5
    ENDP

    ;
    ; Generate a clarinet
    ; R3 = Frequency
    ; R1 = Waveform position
    ;
    ; Output:
    ; R3 = Frequency
    ; R1 = Volume
    ;
_clarinet_instrument:    PROC
    ADDI #_clarinet_vibrato,R1
    ADD@ R1,R3
    CLRC
    RRC R3,1    ; Duplicates frequency
    ADCR R3
    ADDI #_clarinet_volume-_clarinet_vibrato,R1
    MVI@ R1,R1
    IF DEFINED intybasic_music_volume
    B _global_volume
    ELSE
    JR R5
    ENDI
    ENDP

_clarinet_vibrato:    PROC
    DECLE 0,0,0,0
    DECLE -2,-4,-2,0
    DECLE 2,4,2,0
    DECLE -2,-4,-2,0
    DECLE 2,4,2,0
    DECLE -2,-4,-2,0
    ENDP

_clarinet_volume:    PROC
    DECLE 13,14,14,13,13,12,12,12
    DECLE 11,11,11,11,12,12,12,12
    DECLE 11,11,11,11,12,12,12,12
    ENDP

    ;
    ; Generates a flute
    ; R3 = Frequency
    ; R1 = Waveform position
    ;
    ; Output:
    ; R3 = Frequency
    ; R1 = Volume
    ;
_flute_instrument:    PROC
    ADDI #_flute_vibrato,R1
    ADD@ R1,R3
    ADDI #_flute_volume-_flute_vibrato,R1
    MVI@ R1,R1
    IF DEFINED intybasic_music_volume
    B _global_volume
    ELSE
    JR R5
    ENDI
    ENDP

_flute_vibrato:    PROC
    DECLE 0,0,0,0
    DECLE 0,1,2,1
    DECLE 0,1,2,1
    DECLE 0,1,2,1
    DECLE 0,1,2,1
    DECLE 0,1,2,1
    ENDP

_flute_volume:    PROC
    DECLE 10,12,13,13,12,12,12,12
    DECLE 11,11,11,11,10,10,10,10
    DECLE 11,11,11,11,10,10,10,10
    ENDP

    IF DEFINED intybasic_music_volume

_global_volume:    PROC
    MVI _music_vol,R2
    ANDI #$0F,R2
    SLL R2,2
    SLL R2,2
    ADDR R1,R2
    ADDI #@@table,R2
    MVI@ R2,R1
    JR R5

@@table:
    DECLE 0,0,0,0,0,0,0,0,0,0,0,0,0,0,0,0
    DECLE 0,0,0,0,0,0,0,1,1,1,1,1,1,1,1,1
    DECLE 0,0,0,0,1,1,1,1,1,1,1,2,2,2,2,2
    DECLE 0,0,0,1,1,1,1,1,2,2,2,2,2,3,3,3
    DECLE 0,0,1,1,1,1,2,2,2,2,3,3,3,4,4,4
    DECLE 0,0,1,1,1,2,2,2,3,3,3,4,4,4,5,5
    DECLE 0,0,1,1,2,2,2,3,3,4,4,4,5,5,6,6
    DECLE 0,1,1,1,2,2,3,3,4,4,5,5,6,6,7,7
    DECLE 0,1,1,2,2,3,3,4,4,5,5,6,6,7,8,8
    DECLE 0,1,1,2,2,3,4,4,5,5,6,7,7,8,8,9
    DECLE 0,1,1,2,3,3,4,5,5,6,7,7,8,9,9,10
    DECLE 0,1,2,2,3,4,4,5,6,7,7,8,9,10,10,11
    DECLE 0,1,2,2,3,4,5,6,6,7,8,9,10,10,11,12
    DECLE 0,1,2,3,4,4,5,6,7,8,9,10,10,11,12,13
    DECLE 0,1,2,3,4,5,6,7,8,8,9,10,11,12,13,14
    DECLE 0,1,2,3,4,5,6,7,8,9,10,11,12,13,14,15

    ENDP

    ENDI

    IF DEFINED intybasic_music_ecs
    ;
    ; Emits sound for ECS
    ;
_emit_sound_ecs:    PROC
    MOVR R5,R1
    MVI _music_mode,R2
    SARC R2,1
    BEQ @@6
    MVII #_music2_freq10,R4
    MVII #$00F0,R5
    B _emit_sound.0

@@6:    JR R1

    ENDP

    ENDI

    ;
    ; Emits sound
    ;
_emit_sound:    PROC
    MOVR R5,R1
    MVI _music_mode,R2
    SARC R2,1
    BEQ @@6
    MVII #_music_freq10,R4
    MVII #$01F0,R5
@@0:
    MVI@ R4,R0
    MVO@ R0,R5    ; $01F0 - Channel A Period (Low 8 bits of 12)
    MVI@ R4,R0
    MVO@ R0,R5    ; $01F1 - Channel B Period (Low 8 bits of 12)
    DECR R2
    BEQ @@1
    MVI@ R4,R0
    MVO@ R0,R5    ; $01F2 - Channel C Period (Low 8 bits of 12)
    INCR R5        ; Avoid $01F3 - Enveloped Period (Low 8 bits of 16)
    MVI@ R4,R0
    MVO@ R0,R5    ; $01F4 - Channel A Period (High 4 bits of 12)
    MVI@ R4,R0
    MVO@ R0,R5    ; $01F5 - Channel B Period (High 4 bits of 12)
    MVI@ R4,R0
    MVO@ R0,R5    ; $01F6 - Channel C Period (High 4 bits of 12)
    INCR R5        ; Avoid $01F7 - Envelope Period (High 8 bits of 16)
    BC @@2        ; Jump if playing with drums
    ADDI #2,R4
    ADDI #3,R5
    B @@3

@@2:    MVI@ R4,R0
    MVO@ R0,R5    ; $01F8 - Enable Noise/Tone (bits 3-5 Noise : 0-2 Tone)
    MVI@ R4,R0
    MVO@ R0,R5    ; $01F9 - Noise Period (5 bits)
    INCR R5        ; Avoid $01FA - Envelope Type (4 bits)
@@3:    MVI@ R4,R0
    MVO@ R0,R5    ; $01FB - Channel A Volume
    MVI@ R4,R0
    MVO@ R0,R5    ; $01FC - Channel B Volume
    MVI@ R4,R0
    MVO@ R0,R5    ; $01FD - Channel C Volume
    JR R1

@@1:    INCR R4
    INCR R5        ; Avoid $01F2 and $01F3
    INCR R5        ; Cannot use ADDI
    MVI@ R4,R0
    MVO@ R0,R5    ; $01F4 - Channel A Period (High 4 bits of 12)
    MVI@ R4,R0
    MVO@ R0,R5    ; $01F5 - Channel B Period (High 4 bits of 12)
    INCR R4
    INCR R5        ; Avoid $01F6 and $01F7
    INCR R5        ; Cannot use ADDI
    BC @@4        ; Jump if playing with drums
    ADDI #2,R4
    ADDI #3,R5
    B @@5

@@4:    MVI@ R4,R0
    MVO@ R0,R5    ; $01F8 - Enable Noise/Tone (bits 3-5 Noise : 0-2 Tone)
    MVI@ R4,R0
    MVO@ R0,R5    ; $01F9 - Noise Period (5 bits)
    INCR R5        ; Avoid $01FA - Envelope Type (4 bits)
@@5:    MVI@ R4,R0
    MVO@ R0,R5    ; $01FB - Channel A Volume
    MVI@ R4,R0
    MVO@ R0,R5    ; $01FC - Channel B Volume
@@6:    JR R1
    ENDP

    ;
    ; Activates drum
    ;
_activate_drum:    PROC
    IF DEFINED intybasic_music_volume
    BEGIN
    ENDI
    MVI _music_mode,R2
    SARC R2,1    ; PLAY NO DRUMS?
    BNC @@0        ; Yes, jump
    MVI _music_vol1,R0
    TSTR R0
    BNE @@1
    MVII #11,R1
    IF DEFINED intybasic_music_volume
    CALL _global_volume
    ENDI
    MVO R1,_music_vol1
    MVI _music_mix,R0
    ANDI #$F6,R0
    XORI #$01,R0
    MVO R0,_music_mix
    IF DEFINED intybasic_music_volume
    RETURN
    ELSE
    JR R5
    ENDI

@@1:    MVI _music_vol2,R0
    TSTR R0
    BNE @@2
    MVII #11,R1
    IF DEFINED intybasic_music_volume
    CALL _global_volume
    ENDI
    MVO R1,_music_vol2
    MVI _music_mix,R0
    ANDI #$ED,R0
    XORI #$02,R0
    MVO R0,_music_mix
    IF DEFINED intybasic_music_volume
    RETURN
    ELSE
    JR R5
    ENDI

@@2:    DECR R2        ; PLAY SIMPLE?
    BEQ @@3        ; Yes, jump
    MVI _music_vol3,R0
    TSTR R0
    BNE @@3
    MVII #11,R1
    IF DEFINED intybasic_music_volume
    CALL _global_volume
    ENDI
    MVO R1,_music_vol3
    MVI _music_mix,R0
    ANDI #$DB,R0
    XORI #$04,R0
    MVO R0,_music_mix
    IF DEFINED intybasic_music_volume
    RETURN
    ELSE
    JR R5
    ENDI

@@3:    MVI _music_mix,R0
    ANDI #$EF,R0
    MVO R0,_music_mix
@@0:
    IF DEFINED intybasic_music_volume
    RETURN
    ELSE
    JR R5
    ENDI

    ENDP

    IF DEFINED intybasic_music_ecs
    ;
    ; Activates drum
    ;
_activate_drum_ecs:    PROC
    IF DEFINED intybasic_music_volume
    BEGIN
    ENDI
    MVI _music_mode,R2
    SARC R2,1    ; PLAY NO DRUMS?
    BNC @@0        ; Yes, jump
    MVI _music2_vol1,R0
    TSTR R0
    BNE @@1
    MVII #11,R1
    IF DEFINED intybasic_music_volume
    CALL _global_volume
    ENDI
    MVO R1,_music2_vol1
    MVI _music2_mix,R0
    ANDI #$F6,R0
    XORI #$01,R0
    MVO R0,_music2_mix
    IF DEFINED intybasic_music_volume
    RETURN
    ELSE
    JR R5
    ENDI

@@1:    MVI _music2_vol2,R0
    TSTR R0
    BNE @@2
    MVII #11,R1
    IF DEFINED intybasic_music_volume
    CALL _global_volume
    ENDI
    MVO R1,_music2_vol2
    MVI _music2_mix,R0
    ANDI #$ED,R0
    XORI #$02,R0
    MVO R0,_music2_mix
    IF DEFINED intybasic_music_volume
    RETURN
    ELSE
    JR R5
    ENDI

@@2:    DECR R2        ; PLAY SIMPLE?
    BEQ @@3        ; Yes, jump
    MVI _music2_vol3,R0
    TSTR R0
    BNE @@3
    MVII #11,R1
    IF DEFINED intybasic_music_volume
    CALL _global_volume
    ENDI
    MVO R1,_music2_vol3
    MVI _music2_mix,R0
    ANDI #$DB,R0
    XORI #$04,R0
    MVO R0,_music2_mix
    IF DEFINED intybasic_music_volume
    RETURN
    ELSE
    JR R5
    ENDI

@@3:    MVI _music2_mix,R0
    ANDI #$EF,R0
    MVO R0,_music2_mix
@@0:
    IF DEFINED intybasic_music_volume
    RETURN
    ELSE
    JR R5
    ENDI

    ENDP

    ENDI

    ENDI

    IF DEFINED intybasic_numbers

    ;
    ; Following code from as1600 libraries, prnum16.asm
    ; Public domain by Joseph Zbiciak
    ;

;* ======================================================================== *;
;*  These routines are placed into the public domain by their author.  All  *;
;*  copyright rights are hereby relinquished on the routines and data in    *;
;*  this file.  -- Joseph Zbiciak, 2008                     *;
;* ======================================================================== *;

;; ======================================================================== ;;
;;  _PW10                                   ;;
;;      Lookup table holding the first 5 powers of 10 (1 thru 10000) as     ;;
;;      16-bit numbers.                             ;;
;; ======================================================================== ;;
_PW10   PROC    ; 0 thru 10000
    DECLE   10000, 1000, 100, 10, 1, 0
    ENDP

;; ======================================================================== ;;
;;  PRNUM16.l     -- Print an unsigned 16-bit number left-justified.    ;;
;;  PRNUM16.b     -- Print an unsigned 16-bit number with leading blanks.   ;;
;;  PRNUM16.z     -- Print an unsigned 16-bit number with leading zeros.    ;;
;;                                      ;;
;;  AUTHOR                                  ;;
;;      Joseph Zbiciak  <im14u2c AT globalcrossing DOT net>         ;;
;;                                      ;;
;;  REVISION HISTORY                            ;;
;;      30-Mar-2003 Initial complete revision                   ;;
;;                                      ;;
;;  INPUTS for all variants                         ;;
;;      R0  Number to print.                        ;;
;;      R2  Width of field.  Ignored by PRNUM16.l.              ;;
;;      R3  Format word, added to digits to set the color.          ;;
;;      Note:  Bit 15 MUST be cleared when building with PRNUM32.       ;;
;;      R4  Pointer to location on screen to print number           ;;
;;                                      ;;
;;  OUTPUTS                                 ;;
;;      R0  Zeroed                              ;;
;;      R1  Unmodified                              ;;
;;      R2  Unmodified                              ;;
;;      R3  Unmodified                              ;;
;;      R4  Points to first character after field.              ;;
;;                                      ;;
;;  DESCRIPTION                                 ;;
;;      These routines print unsigned 16-bit numbers in a field up to 5     ;;
;;      positions wide.  The number is printed either in left-justified     ;;
;;      or right-justified format.  Right-justified numbers are padded      ;;
;;      with leading blanks or leading zeros.  Left-justified numbers       ;;
;;      are not padded on the right.                    ;;
;;                                      ;;
;;      This code handles fields wider than 5 characters, padding with      ;;
;;      zeros or blanks as necessary.                       ;;
;;                                      ;;
;;          Routine      Value(hex)     Field    Output         ;;
;;          ----------   ----------   ----------   ----------       ;;
;;          PRNUM16.l      $0045     n/a    "69"        ;;
;;          PRNUM16.b      $0045      4     "  69"          ;;
;;          PRNUM16.b      $0045      6     "    69"        ;;
;;          PRNUM16.z      $0045      4     "0069"          ;;
;;          PRNUM16.z      $0045      6     "000069"        ;;
;;                                      ;;
;;  TECHNIQUES                                  ;;
;;      This routine uses repeated subtraction to divide the number     ;;
;;      to display by various powers of 10.  This is cheaper than a     ;;
;;      full divide, at least when the input number is large.  It's     ;;
;;      also easier to get right.  :-)                      ;;
;;                                      ;;
;;      The printing routine first pads out fields wider than 5 spaces      ;;
;;      with zeros or blanks as requested.  It then scans the power-of-10   ;;
;;      table looking for the first power of 10 that is <= the number to    ;;
;;      display.  While scanning for this power of 10, it outputs leading   ;;
;;      blanks or zeros, if requested.  This eliminates "leading digit"     ;;
;;      logic from the main digit loop.                     ;;
;;                                      ;;
;;      Once in the main digit loop, we discover the value of each digit    ;;
;;      by repeated subtraction.  We build up our digit value while     ;;
;;      subtracting the power-of-10 repeatedly.  We iterate until we go     ;;
;;      a step too far, and then we add back on power-of-10 to restore      ;;
;;      the remainder.                              ;;
;;                                      ;;
;;  NOTES                                   ;;
;;      The left-justified variant ignores field width.             ;;
;;                                      ;;
;;      The code is fully reentrant.                    ;;
;;                                      ;;
;;      This code does not handle numbers which are too large to be     ;;
;;      displayed in the provided field.  If the number is too large,       ;;
;;      non-digit characters will be displayed in the initial digit     ;;
;;      position.  Also, the run time of this routine may get excessively   ;;
;;      large, depending on the magnitude of the overflow.          ;;
;;                                      ;;
;;      When using with PRNUM32, one must either include PRNUM32 before     ;;
;;      this function, or define the symbol _WITH_PRNUM32.  PRNUM32     ;;
;;      needs a tiny bit of support from PRNUM16 to handle numbers in       ;;
;;      the range 65536...99999 correctly.                  ;;
;;                                      ;;
;;  CODESIZE                                ;;
;;      73 words, including power-of-10 table                   ;;
;;      80 words, if compiled with PRNUM32.                 ;;
;;                                      ;;
;;      To save code size, you can define the following symbols to omit     ;;
;;      some variants:                              ;;
;;                                      ;;
;;      _NO_PRNUM16.l:   Disables PRNUM16.l.  Saves 10 words        ;;
;;      _NO_PRNUM16.b:   Disables PRNUM16.b.  Saves 3 words.        ;;
;;                                      ;;
;;      Defining both symbols saves 17 words total, because it omits    ;;
;;      some code shared by both routines.                  ;;
;;                                      ;;
;;  STACK USAGE                                 ;;
;;      This function uses up to 4 words of stack space.            ;;
;; ======================================================================== ;;

PRNUM16 PROC


    ;; ---------------------------------------------------------------- ;;
    ;;  PRNUM16.l:  Print unsigned, left-justified.             ;;
    ;; ---------------------------------------------------------------- ;;
@@l:    PSHR    R5          ; save return address
@@l1:   MVII    #$1,    R5      ; set R5 to 1 to counteract screen ptr update
                ; in the 'find initial power of 10' loop
    PSHR    R2
    MVII    #5,     R2      ; force effective field width to 5.
    B       @@z2

    ;; ---------------------------------------------------------------- ;;
    ;;  PRNUM16.b:  Print unsigned with leading blanks.         ;;
    ;; ---------------------------------------------------------------- ;;
@@b:    PSHR    R5
@@b1:   CLRR    R5          ; let the blank loop do its thing
    INCR    PC          ; skip the PSHR R5

    ;; ---------------------------------------------------------------- ;;
    ;;  PRNUM16.z:  Print unsigned with leading zeros.          ;;
    ;; ---------------------------------------------------------------- ;;
@@z:    PSHR    R5
@@z1:   PSHR    R2
@@z2:   PSHR    R1

    ;; ---------------------------------------------------------------- ;;
    ;;  Find the initial power of 10 to use for display.        ;;
    ;;  Note:  For fields wider than 5, fill the extra spots above 5    ;;
    ;;  with blanks or zeros as needed.                 ;;
    ;; ---------------------------------------------------------------- ;;
    MVII    #_PW10+5,R1     ; Point to end of power-of-10 table
    SUBR    R2,     R1      ; Subtract the field width to get right power
    PSHR    R3          ; save format word

    CMPI    #2,     R5      ; are we leading with zeros?
    BNC     @@lblnk     ; no:  then do the loop w/ blanks

    CLRR    R5          ; force R5==0
    ADDI    #$80,   R3      ; yes: do the loop with zeros
    B       @@lblnk


@@llp   MVO@    R3,     R4      ; print a blank/zero

    SUBR    R5,     R4      ; rewind pointer if needed.

    INCR    R1          ; get next power of 10
@@lblnk DECR    R2          ; decrement available digits
    BEQ     @@ldone
    CMPI    #5,     R2      ; field too wide?
    BGE     @@llp       ; just force blanks/zeros 'till we're narrower.
    CMP@    R1,     R0      ; Is this power of 10 too big?
    BNC     @@llp       ; Yes:  Put a blank and go to next

@@ldone PULR    R3          ; restore format word

    ;; ---------------------------------------------------------------- ;;
    ;;  The digit loop prints at least one digit.  It discovers digits  ;;
    ;;  by repeated subtraction.                    ;;
    ;; ---------------------------------------------------------------- ;;
@@digit TSTR    R0          ; If the number is zero, print zero and leave
    BNEQ    @@dig1      ; no: print the number

    MOVR    R3,     R5      ;\
    ADDI    #$80,   R5      ; |-- print a 0 there.
    MVO@    R5,     R4      ;/
    B       @@done

@@dig1:

@@nxdig MOVR    R3,     R5      ; save display format word
@@cont: ADDI    #$80-8, R5      ; start our digit as one just before '0'
@@spcl:

    ;; ---------------------------------------------------------------- ;;
    ;;  Divide by repeated subtraction.  This divide is constructed     ;;
    ;;  to go "one step too far" and then back up.              ;;
    ;; ---------------------------------------------------------------- ;;
@@div:  ADDI    #8,     R5      ; increment our digit
    SUB@    R1,     R0      ; subtract power of 10
    BC      @@div       ; loop until we go too far
    ADD@    R1,     R0      ; add back the extra power of 10.

    MVO@    R5,     R4      ; display the digit.

    INCR    R1          ; point to next power of 10
    DECR    R2          ; any room left in field?
    BPL     @@nxdig     ; keep going until R2 < 0.

@@done: PULR    R1          ; restore R1
    PULR    R2          ; restore R2
    PULR    PC          ; return

    ENDP

    ENDI

    IF DEFINED intybasic_voice
;;==========================================================================;;
;;  SP0256-AL2 Allophones                           ;;
;;                                      ;;
;;  This file contains the allophone set that was obtained from an      ;;
;;  SP0256-AL2.  It is being provided for your convenience.         ;;
;;                                      ;;
;;  The directory "al2" contains a series of assembly files, each one       ;;
;;  containing a single allophone.  This series of files may be useful in   ;;
;;  situations where space is at a premium.                 ;;
;;                                      ;;
;;  Consult the Archer SP0256-AL2 documentation (under doc/programming)     ;;
;;  for more information about SP0256-AL2's allophone library.          ;;
;;                                      ;;
;; ------------------------------------------------------------------------ ;;
;;                                      ;;
;;  Copyright information:                          ;;
;;                                      ;;
;;  The allophone data below was extracted from the SP0256-AL2 ROM image.   ;;
;;  The SP0256-AL2 allophones are NOT in the public domain, nor are they    ;;
;;  placed under the GNU General Public License.  This program is       ;;
;;  distributed in the hope that it will be useful, but WITHOUT ANY     ;;
;;  WARRANTY; without even the implied warranty of MERCHANTABILITY or       ;;
;;  FITNESS FOR A PARTICULAR PURPOSE.                       ;;
;;                                      ;;
;;  Microchip, Inc. retains the copyright to the data and algorithms    ;;
;;  contained in the SP0256-AL2.  This speech data is distributed with      ;;
;;  explicit permission from Microchip, Inc.  All such redistributions      ;;
;;  must retain this notice of copyright.                   ;;
;;                                      ;;
;;  No copyright claims are made on this data by the author(s) of SDK1600.  ;;
;;  Please see http://spatula-city.org/~im14u2c/sp0256-al2/ for details.    ;;
;;                                      ;;
;;==========================================================================;;

;; ------------------------------------------------------------------------ ;;
_AA:
    DECLE   _AA.end - _AA - 1
    DECLE   $0318, $014C, $016F, $02CE, $03AF, $015F, $01B1, $008E
    DECLE   $0088, $0392, $01EA, $024B, $03AA, $039B, $000F, $0000
_AA.end:  ; 16 decles
;; ------------------------------------------------------------------------ ;;
_AE1:
    DECLE   _AE1.end - _AE1 - 1
    DECLE   $0118, $038E, $016E, $01FC, $0149, $0043, $026F, $036E
    DECLE   $01CC, $0005, $0000
_AE1.end:  ; 11 decles
;; ------------------------------------------------------------------------ ;;
_AO:
    DECLE   _AO.end - _AO - 1
    DECLE   $0018, $010E, $016F, $0225, $00C6, $02C4, $030F, $0160
    DECLE   $024B, $0005, $0000
_AO.end:  ; 11 decles
;; ------------------------------------------------------------------------ ;;
_AR:
    DECLE   _AR.end - _AR - 1
    DECLE   $0218, $010C, $016E, $001E, $000B, $0091, $032F, $00DE
    DECLE   $018B, $0095, $0003, $0238, $0027, $01E0, $03E8, $0090
    DECLE   $0003, $01C7, $0020, $03DE, $0100, $0190, $01CA, $02AB
    DECLE   $00B7, $004A, $0386, $0100, $0144, $02B6, $0024, $0320
    DECLE   $0011, $0041, $01DF, $0316, $014C, $016E, $001E, $00C4
    DECLE   $02B2, $031E, $0264, $02AA, $019D, $01BE, $000B, $00F0
    DECLE   $006A, $01CE, $00D6, $015B, $03B5, $03E4, $0000, $0380
    DECLE   $0007, $0312, $03E8, $030C, $016D, $02EE, $0085, $03C2
    DECLE   $03EC, $0283, $024A, $0005, $0000
_AR.end:  ; 69 decles
;; ------------------------------------------------------------------------ ;;
_AW:
    DECLE   _AW.end - _AW - 1
    DECLE   $0010, $01CE, $016E, $02BE, $0375, $034F, $0220, $0290
    DECLE   $008A, $026D, $013F, $01D5, $0316, $029F, $02E2, $018A
    DECLE   $0170, $0035, $00BD, $0000, $0000
_AW.end:  ; 21 decles
;; ------------------------------------------------------------------------ ;;
_AX:
    DECLE   _AX.end - _AX - 1
    DECLE   $0218, $02CD, $016F, $02F5, $0386, $00C2, $00CD, $0094
    DECLE   $010C, $0005, $0000
_AX.end:  ; 11 decles
;; ------------------------------------------------------------------------ ;;
_AY:
    DECLE   _AY.end - _AY - 1
    DECLE   $0110, $038C, $016E, $03B7, $03B3, $02AF, $0221, $009E
    DECLE   $01AA, $01B3, $00BF, $02E7, $025B, $0354, $00DA, $017F
    DECLE   $018A, $03F3, $00AF, $02D5, $0356, $027F, $017A, $01FB
    DECLE   $011E, $01B9, $03E5, $029F, $025A, $0076, $0148, $0124
    DECLE   $003D, $0000
_AY.end:  ; 34 decles
;; ------------------------------------------------------------------------ ;;
_BB1:
    DECLE   _BB1.end - _BB1 - 1
    DECLE   $0318, $004C, $016C, $00FB, $00C7, $0144, $002E, $030C
    DECLE   $010E, $018C, $01DC, $00AB, $00C9, $0268, $01F7, $021D
    DECLE   $01B3, $0098, $0000
_BB1.end:  ; 19 decles
;; ------------------------------------------------------------------------ ;;
_BB2:
    DECLE   _BB2.end - _BB2 - 1
    DECLE   $00F4, $0046, $0062, $0200, $0221, $03E4, $0087, $016F
    DECLE   $02A6, $02B7, $0212, $0326, $0368, $01BF, $0338, $0196
    DECLE   $0002
_BB2.end:  ; 17 decles
;; ------------------------------------------------------------------------ ;;
_CH:
    DECLE   _CH.end - _CH - 1
    DECLE   $00F5, $0146, $0052, $0000, $032A, $0049, $0032, $02F2
    DECLE   $02A5, $0000, $026D, $0119, $0124, $00F6, $0000
_CH.end:  ; 15 decles
;; ------------------------------------------------------------------------ ;;
_DD1:
    DECLE   _DD1.end - _DD1 - 1
    DECLE   $0318, $034C, $016E, $0397, $01B9, $0020, $02B1, $008E
    DECLE   $0349, $0291, $01D8, $0072, $0000
_DD1.end:  ; 13 decles
;; ------------------------------------------------------------------------ ;;
_DD2:
    DECLE   _DD2.end - _DD2 - 1
    DECLE   $00F4, $00C6, $00F2, $0000, $0129, $00A6, $0246, $01F3
    DECLE   $02C6, $02B7, $028E, $0064, $0362, $01CF, $0379, $01D5
    DECLE   $0002
_DD2.end:  ; 17 decles
;; ------------------------------------------------------------------------ ;;
_DH1:
    DECLE   _DH1.end - _DH1 - 1
    DECLE   $0018, $034F, $016D, $030B, $0306, $0363, $017E, $006A
    DECLE   $0164, $019E, $01DA, $00CB, $00E8, $027A, $03E8, $01D7
    DECLE   $0173, $00A1, $0000
_DH1.end:  ; 19 decles
;; ------------------------------------------------------------------------ ;;
_DH2:
    DECLE   _DH2.end - _DH2 - 1
    DECLE   $0119, $034C, $016D, $030B, $0306, $0363, $017E, $006A
    DECLE   $0164, $019E, $01DA, $00CB, $00E8, $027A, $03E8, $01D7
    DECLE   $0173, $00A1, $0000
_DH2.end:  ; 19 decles
;; ------------------------------------------------------------------------ ;;
_EH:
    DECLE   _EH.end - _EH - 1
    DECLE   $0218, $02CD, $016F, $0105, $014B, $0224, $02CF, $0274
    DECLE   $014C, $0005, $0000
_EH.end:  ; 11 decles
;; ------------------------------------------------------------------------ ;;
_EL:
    DECLE   _EL.end - _EL - 1
    DECLE   $0118, $038D, $016E, $011C, $008B, $03D2, $030F, $0262
    DECLE   $006C, $019D, $01CC, $022B, $0170, $0078, $03FE, $0018
    DECLE   $0183, $03A3, $010D, $016E, $012E, $00C6, $00C3, $0300
    DECLE   $0060, $000D, $0005, $0000
_EL.end:  ; 28 decles
;; ------------------------------------------------------------------------ ;;
_ER1:
    DECLE   _ER1.end - _ER1 - 1
    DECLE   $0118, $034C, $016E, $001C, $0089, $01C3, $034E, $03E6
    DECLE   $00AB, $0095, $0001, $0000, $03FC, $0381, $0000, $0188
    DECLE   $01DA, $00CB, $00E7, $0048, $03A6, $0244, $016C, $01A8
    DECLE   $03E4, $0000, $0002, $0001, $00FC, $01DA, $02E4, $0000
    DECLE   $0002, $0008, $0200, $0217, $0164, $0000, $000E, $0038
    DECLE   $0014, $01EA, $0264, $0000, $0002, $0048, $01EC, $02F1
    DECLE   $03CC, $016D, $021E, $0048, $00C2, $034E, $036A, $000D
    DECLE   $008D, $000B, $0200, $0047, $0022, $03A8, $0000, $0000
_ER1.end:  ; 64 decles
;; ------------------------------------------------------------------------ ;;
_ER2:
    DECLE   _ER2.end - _ER2 - 1
    DECLE   $0218, $034C, $016E, $001C, $0089, $01C3, $034E, $03E6
    DECLE   $00AB, $0095, $0001, $0000, $03FC, $0381, $0000, $0190
    DECLE   $01D8, $00CB, $00E7, $0058, $01A6, $0244, $0164, $02A9
    DECLE   $0024, $0000, $0000, $0007, $0201, $02F8, $02E4, $0000
    DECLE   $0002, $0001, $00FC, $02DA, $0024, $0000, $0002, $0008
    DECLE   $0200, $0217, $0024, $0000, $000E, $0038, $0014, $03EA
    DECLE   $03A4, $0000, $0002, $0048, $01EC, $03F1, $038C, $016D
    DECLE   $021E, $0048, $00C2, $034E, $036A, $000D, $009D, $0003
    DECLE   $0200, $0047, $0022, $03A8, $0000, $0000
_ER2.end:  ; 70 decles
;; ------------------------------------------------------------------------ ;;
_EY:
    DECLE   _EY.end - _EY - 1
    DECLE   $0310, $038C, $016E, $02A7, $00BB, $0160, $0290, $0094
    DECLE   $01CA, $03A9, $00C1, $02D7, $015B, $01D4, $03CE, $02FF
    DECLE   $00EA, $03E7, $0041, $0277, $025B, $0355, $03C9, $0103
    DECLE   $02EA, $03E4, $003F, $0000
_EY.end:  ; 28 decles
;; ------------------------------------------------------------------------ ;;
_FF:
    DECLE   _FF.end - _FF - 1
    DECLE   $0119, $03C8, $0000, $00A7, $0094, $0138, $01C6, $0000
_FF.end:  ; 8 decles
;; ------------------------------------------------------------------------ ;;
_GG1:
    DECLE   _GG1.end - _GG1 - 1
    DECLE   $00F4, $00C6, $00C2, $0200, $0015, $03FE, $0283, $01FD
    DECLE   $01E6, $00B7, $030A, $0364, $0331, $017F, $033D, $0215
    DECLE   $0002
_GG1.end:  ; 17 decles
;; ------------------------------------------------------------------------ ;;
_GG2:
    DECLE   _GG2.end - _GG2 - 1
    DECLE   $00F4, $0106, $0072, $0300, $0021, $0308, $0039, $0173
    DECLE   $00C6, $00B7, $037E, $03A3, $0319, $0177, $0036, $0217
    DECLE   $0002
_GG2.end:  ; 17 decles
;; ------------------------------------------------------------------------ ;;
_GG3:
    DECLE   _GG3.end - _GG3 - 1
    DECLE   $00F8, $0146, $00F2, $0100, $0132, $03A8, $0055, $01F5
    DECLE   $00A6, $02B7, $0291, $0326, $0368, $0167, $023A, $01C6
    DECLE   $0002
_GG3.end:  ; 17 decles
;; ------------------------------------------------------------------------ ;;
_HH1:
    DECLE   _HH1.end - _HH1 - 1
    DECLE   $0218, $01C9, $0000, $0095, $0127, $0060, $01D6, $0213
    DECLE   $0002, $01AE, $033E, $01A0, $03C4, $0122, $0001, $0218
    DECLE   $01E4, $03FD, $0019, $0000
_HH1.end:  ; 20 decles
;; ------------------------------------------------------------------------ ;;
_HH2:
    DECLE   _HH2.end - _HH2 - 1
    DECLE   $0218, $00CB, $0000, $0086, $000F, $0240, $0182, $031A
    DECLE   $02DB, $0008, $0293, $0067, $00BD, $01E0, $0092, $000C
    DECLE   $0000
_HH2.end:  ; 17 decles
;; ------------------------------------------------------------------------ ;;
_IH:
    DECLE   _IH.end - _IH - 1
    DECLE   $0118, $02CD, $016F, $0205, $0144, $02C3, $00FE, $031A
    DECLE   $000D, $0005, $0000
_IH.end:  ; 11 decles
;; ------------------------------------------------------------------------ ;;
_IY:
    DECLE   _IY.end - _IY - 1
    DECLE   $0318, $02CC, $016F, $0008, $030B, $01C3, $0330, $0178
    DECLE   $002B, $019D, $01F6, $018B, $01E1, $0010, $020D, $0358
    DECLE   $015F, $02A4, $02CC, $016F, $0109, $030B, $0193, $0320
    DECLE   $017A, $034C, $009C, $0017, $0001, $0200, $03C1, $0020
    DECLE   $00A7, $001D, $0001, $0104, $003D, $0040, $01A7, $01CA
    DECLE   $018B, $0160, $0078, $01F6, $0343, $01C7, $0090, $0000
_IY.end:  ; 48 decles
;; ------------------------------------------------------------------------ ;;
_JH:
    DECLE   _JH.end - _JH - 1
    DECLE   $0018, $0149, $0001, $00A4, $0321, $0180, $01F4, $039A
    DECLE   $02DC, $023C, $011A, $0047, $0200, $0001, $018E, $034E
    DECLE   $0394, $0356, $02C1, $010C, $03FD, $0129, $00B7, $01BA
    DECLE   $0000
_JH.end:  ; 25 decles
;; ------------------------------------------------------------------------ ;;
_KK1:
    DECLE   _KK1.end - _KK1 - 1
    DECLE   $00F4, $00C6, $00D2, $0000, $023A, $03E0, $02D1, $02E5
    DECLE   $0184, $0200, $0041, $0210, $0188, $00C5, $0000
_KK1.end:  ; 15 decles
;; ------------------------------------------------------------------------ ;;
_KK2:
    DECLE   _KK2.end - _KK2 - 1
    DECLE   $021D, $023C, $0211, $003C, $0180, $024D, $0008, $032B
    DECLE   $025B, $002D, $01DC, $01E3, $007A, $0000
_KK2.end:  ; 14 decles
;; ------------------------------------------------------------------------ ;;
_KK3:
    DECLE   _KK3.end - _KK3 - 1
    DECLE   $00F7, $0046, $01D2, $0300, $0131, $006C, $006E, $00F1
    DECLE   $00E4, $0000, $025A, $010D, $0110, $01F9, $014A, $0001
    DECLE   $00B5, $01A2, $00D8, $01CE, $0000
_KK3.end:  ; 21 decles
;; ------------------------------------------------------------------------ ;;
_LL:
    DECLE   _LL.end - _LL - 1
    DECLE   $0318, $038C, $016D, $029E, $0333, $0260, $0221, $0294
    DECLE   $01C4, $0299, $025A, $00E6, $014C, $012C, $0031, $0000
_LL.end:  ; 16 decles
;; ------------------------------------------------------------------------ ;;
_MM:
    DECLE   _MM.end - _MM - 1
    DECLE   $0210, $034D, $016D, $03F5, $00B0, $002E, $0220, $0290
    DECLE   $03CE, $02B6, $03AA, $00F3, $00CF, $015D, $016E, $0000
_MM.end:  ; 16 decles
;; ------------------------------------------------------------------------ ;;
_NG1:
    DECLE   _NG1.end - _NG1 - 1
    DECLE   $0118, $03CD, $016E, $00DC, $032F, $01BF, $01E0, $0116
    DECLE   $02AB, $029A, $0358, $01DB, $015B, $01A7, $02FD, $02B1
    DECLE   $03D2, $0356, $0000
_NG1.end:  ; 19 decles
;; ------------------------------------------------------------------------ ;;
_NN1:
    DECLE   _NN1.end - _NN1 - 1
    DECLE   $0318, $03CD, $016C, $0203, $0306, $03C3, $015F, $0270
    DECLE   $002A, $009D, $000D, $0248, $01B4, $0120, $01E1, $00C8
    DECLE   $0003, $0040, $0000, $0080, $015F, $0006, $0000
_NN1.end:  ; 23 decles
;; ------------------------------------------------------------------------ ;;
_NN2:
    DECLE   _NN2.end - _NN2 - 1
    DECLE   $0018, $034D, $016D, $0203, $0306, $03C3, $015F, $0270
    DECLE   $002A, $0095, $0003, $0248, $01B4, $0120, $01E1, $0090
    DECLE   $000B, $0040, $0000, $0080, $015F, $019E, $01F6, $028B
    DECLE   $00E0, $0266, $03F6, $01D8, $0143, $01A8, $0024, $00C0
    DECLE   $0080, $0000, $01E6, $0321, $0024, $0260, $000A, $0008
    DECLE   $03FE, $0000, $0000
_NN2.end:  ; 43 decles
;; ------------------------------------------------------------------------ ;;
_OR2:
    DECLE   _OR2.end - _OR2 - 1
    DECLE   $0218, $018C, $016D, $02A6, $03AB, $004F, $0301, $0390
    DECLE   $02EA, $0289, $0228, $0356, $01CF, $02D5, $0135, $007D
    DECLE   $02B5, $02AF, $024A, $02E2, $0153, $0167, $0333, $02A9
    DECLE   $02B3, $039A, $0351, $0147, $03CD, $0339, $02DA, $0000
_OR2.end:  ; 32 decles
;; ------------------------------------------------------------------------ ;;
_OW:
    DECLE   _OW.end - _OW - 1
    DECLE   $0310, $034C, $016E, $02AE, $03B1, $00CF, $0304, $0192
    DECLE   $018A, $022B, $0041, $0277, $015B, $0395, $03D1, $0082
    DECLE   $03CE, $00B6, $03BB, $02DA, $0000
_OW.end:  ; 21 decles
;; ------------------------------------------------------------------------ ;;
_OY:
    DECLE   _OY.end - _OY - 1
    DECLE   $0310, $014C, $016E, $02A6, $03AF, $00CF, $0304, $0192
    DECLE   $03CA, $01A8, $007F, $0155, $02B4, $027F, $00E2, $036A
    DECLE   $031F, $035D, $0116, $01D5, $02F4, $025F, $033A, $038A
    DECLE   $014F, $01B5, $03D5, $0297, $02DA, $03F2, $0167, $0124
    DECLE   $03FB, $0001
_OY.end:  ; 34 decles
;; ------------------------------------------------------------------------ ;;
_PA1:
    DECLE   _PA1.end - _PA1 - 1
    DECLE   $00F1, $0000
_PA1.end:  ; 2 decles
;; ------------------------------------------------------------------------ ;;
_PA2:
    DECLE   _PA2.end - _PA2 - 1
    DECLE   $00F4, $0000
_PA2.end:  ; 2 decles
;; ------------------------------------------------------------------------ ;;
_PA3:
    DECLE   _PA3.end - _PA3 - 1
    DECLE   $00F7, $0000
_PA3.end:  ; 2 decles
;; ------------------------------------------------------------------------ ;;
_PA4:
    DECLE   _PA4.end - _PA4 - 1
    DECLE   $00FF, $0000
_PA4.end:  ; 2 decles
;; ------------------------------------------------------------------------ ;;
_PA5:
    DECLE   _PA5.end - _PA5 - 1
    DECLE   $031D, $003F, $0000
_PA5.end:  ; 3 decles
;; ------------------------------------------------------------------------ ;;
_PP:
    DECLE   _PP.end - _PP - 1
    DECLE   $00FD, $0106, $0052, $0000, $022A, $03A5, $0277, $035F
    DECLE   $0184, $0000, $0055, $0391, $00EB, $00CF, $0000
_PP.end:  ; 15 decles
;; ------------------------------------------------------------------------ ;;
_RR1:
    DECLE   _RR1.end - _RR1 - 1
    DECLE   $0118, $01CD, $016C, $029E, $0171, $038E, $01E0, $0190
    DECLE   $0245, $0299, $01AA, $02E2, $01C7, $02DE, $0125, $00B5
    DECLE   $02C5, $028F, $024E, $035E, $01CB, $02EC, $0005, $0000
_RR1.end:  ; 24 decles
;; ------------------------------------------------------------------------ ;;
_RR2:
    DECLE   _RR2.end - _RR2 - 1
    DECLE   $0218, $03CC, $016C, $030C, $02C8, $0393, $02CD, $025E
    DECLE   $008A, $019D, $01AC, $02CB, $00BE, $0046, $017E, $01C2
    DECLE   $0174, $00A1, $01E5, $00E0, $010E, $0007, $0313, $0017
    DECLE   $0000
_RR2.end:  ; 25 decles
;; ------------------------------------------------------------------------ ;;
_SH:
    DECLE   _SH.end - _SH - 1
    DECLE   $0218, $0109, $0000, $007A, $0187, $02E0, $03F6, $0311
    DECLE   $0002, $0126, $0242, $0161, $03E9, $0219, $016C, $0300
    DECLE   $0013, $0045, $0124, $0005, $024C, $005C, $0182, $03C2
    DECLE   $0001
_SH.end:  ; 25 decles
;; ------------------------------------------------------------------------ ;;
_SS:
    DECLE   _SS.end - _SS - 1
    DECLE   $0218, $01CA, $0001, $0128, $001C, $0149, $01C6, $0000
_SS.end:  ; 8 decles
;; ------------------------------------------------------------------------ ;;
_TH:
    DECLE   _TH.end - _TH - 1
    DECLE   $0019, $0349, $0000, $00C6, $0212, $01D8, $01CA, $0000
_TH.end:  ; 8 decles
;; ------------------------------------------------------------------------ ;;
_TT1:
    DECLE   _TT1.end - _TT1 - 1
    DECLE   $00F6, $0046, $0142, $0100, $0042, $0088, $027E, $02EF
    DECLE   $01A4, $0200, $0049, $0290, $00FC, $00E8, $0000
_TT1.end:  ; 15 decles
;; ------------------------------------------------------------------------ ;;
_TT2:
    DECLE   _TT2.end - _TT2 - 1
    DECLE   $00F5, $00C6, $01D2, $0100, $0335, $00E9, $0042, $027A
    DECLE   $02A4, $0000, $0062, $01D1, $014C, $03EA, $02EC, $01E0
    DECLE   $0007, $03A7, $0000
_TT2.end:  ; 19 decles
;; ------------------------------------------------------------------------ ;;
_UH:
    DECLE   _UH.end - _UH - 1
    DECLE   $0018, $034E, $016E, $01FF, $0349, $00D2, $003C, $030C
    DECLE   $008B, $0005, $0000
_UH.end:  ; 11 decles
;; ------------------------------------------------------------------------ ;;
_UW1:
    DECLE   _UW1.end - _UW1 - 1
    DECLE   $0318, $014C, $016F, $029E, $03BD, $03BD, $0271, $0212
    DECLE   $0325, $0291, $016A, $027B, $014A, $03B4, $0133, $0001
_UW1.end:  ; 16 decles
;; ------------------------------------------------------------------------ ;;
_UW2:
    DECLE   _UW2.end - _UW2 - 1
    DECLE   $0018, $034E, $016E, $02F6, $0107, $02C2, $006D, $0090
    DECLE   $03AC, $01A4, $01DC, $03AB, $0128, $0076, $03E6, $0119
    DECLE   $014F, $03A6, $03A5, $0020, $0090, $0001, $02EE, $00BB
    DECLE   $0000
_UW2.end:  ; 25 decles
;; ------------------------------------------------------------------------ ;;
_VV:
    DECLE   _VV.end - _VV - 1
    DECLE   $0218, $030D, $016C, $010B, $010B, $0095, $034F, $03E4
    DECLE   $0108, $01B5, $01BE, $028B, $0160, $00AA, $03E4, $0106
    DECLE   $00EB, $02DE, $014C, $016E, $00F6, $0107, $00D2, $00CD
    DECLE   $0296, $00E4, $0006, $0000
_VV.end:  ; 28 decles
;; ------------------------------------------------------------------------ ;;
_WH:
    DECLE   _WH.end - _WH - 1
    DECLE   $0218, $00C9, $0000, $0084, $038E, $0147, $03A4, $0195
    DECLE   $0000, $012E, $0118, $0150, $02D1, $0232, $01B7, $03F1
    DECLE   $0237, $01C8, $03B1, $0227, $01AE, $0254, $0329, $032D
    DECLE   $01BF, $0169, $019A, $0307, $0181, $028D, $0000
_WH.end:  ; 31 decles
;; ------------------------------------------------------------------------ ;;
_WW:
    DECLE   _WW.end - _WW - 1
    DECLE   $0118, $034D, $016C, $00FA, $02C7, $0072, $03CC, $0109
    DECLE   $000B, $01AD, $019E, $016B, $0130, $0278, $01F8, $0314
    DECLE   $017E, $029E, $014D, $016D, $0205, $0147, $02E2, $001A
    DECLE   $010A, $026E, $0004, $0000
_WW.end:  ; 28 decles
;; ------------------------------------------------------------------------ ;;
_XR2:
    DECLE   _XR2.end - _XR2 - 1
    DECLE   $0318, $034C, $016E, $02A6, $03BB, $002F, $0290, $008E
    DECLE   $004B, $0392, $01DA, $024B, $013A, $01DA, $012F, $00B5
    DECLE   $02E5, $0297, $02DC, $0372, $014B, $016D, $0377, $00E7
    DECLE   $0376, $038A, $01CE, $026B, $02FA, $01AA, $011E, $0071
    DECLE   $00D5, $0297, $02BC, $02EA, $01C7, $02D7, $0135, $0155
    DECLE   $01DD, $0007, $0000
_XR2.end:  ; 43 decles
;; ------------------------------------------------------------------------ ;;
_YR:
    DECLE   _YR.end - _YR - 1
    DECLE   $0318, $03CC, $016E, $0197, $00FD, $0130, $0270, $0094
    DECLE   $0328, $0291, $0168, $007E, $01CC, $02F5, $0125, $02B5
    DECLE   $00F4, $0298, $01DA, $03F6, $0153, $0126, $03B9, $00AB
    DECLE   $0293, $03DB, $0175, $01B9, $0001
_YR.end:  ; 29 decles
;; ------------------------------------------------------------------------ ;;
_YY1:
    DECLE   _YY1.end - _YY1 - 1
    DECLE   $0318, $01CC, $016E, $0015, $00CB, $0263, $0320, $0078
    DECLE   $01CE, $0094, $001F, $0040, $0320, $03BF, $0230, $00A7
    DECLE   $000F, $01FE, $03FC, $01E2, $00D0, $0089, $000F, $0248
    DECLE   $032B, $03FD, $01CF, $0001, $0000
_YY1.end:  ; 29 decles
;; ------------------------------------------------------------------------ ;;
_YY2:
    DECLE   _YY2.end - _YY2 - 1
    DECLE   $0318, $01CC, $016E, $0015, $00CB, $0263, $0320, $0078
    DECLE   $01CE, $0094, $001F, $0040, $0320, $03BF, $0230, $00A7
    DECLE   $000F, $01FE, $03FC, $01E2, $00D0, $0089, $000F, $0248
    DECLE   $032B, $03FD, $01CF, $0199, $01EE, $008B, $0161, $0232
    DECLE   $0004, $0318, $01A7, $0198, $0124, $03E0, $0001, $0001
    DECLE   $030F, $0027, $0000
_YY2.end:  ; 43 decles
;; ------------------------------------------------------------------------ ;;
_ZH:
    DECLE   _ZH.end - _ZH - 1
    DECLE   $0310, $014D, $016E, $00C3, $03B9, $01BF, $0241, $0012
    DECLE   $0163, $00E1, $0000, $0080, $0084, $023F, $003F, $0000
_ZH.end:  ; 16 decles
;; ------------------------------------------------------------------------ ;;
_ZZ:
    DECLE   _ZZ.end - _ZZ - 1
    DECLE   $0218, $010D, $016F, $0225, $0351, $00B5, $02A0, $02EE
    DECLE   $00E9, $014D, $002C, $0360, $0008, $00EC, $004C, $0342
    DECLE   $03D4, $0156, $0052, $0131, $0008, $03B0, $01BE, $0172
    DECLE   $0000
_ZZ.end:  ; 25 decles

;;==========================================================================;;
;;                                      ;;
;;  Copyright information:                          ;;
;;                                      ;;
;;  The above allophone data was extracted from the SP0256-AL2 ROM image.   ;;
;;  The SP0256-AL2 allophones are NOT in the public domain, nor are they    ;;
;;  placed under the GNU General Public License.  This program is       ;;
;;  distributed in the hope that it will be useful, but WITHOUT ANY     ;;
;;  WARRANTY; without even the implied warranty of MERCHANTABILITY or       ;;
;;  FITNESS FOR A PARTICULAR PURPOSE.                       ;;
;;                                      ;;
;;  Microchip, Inc. retains the copyright to the data and algorithms    ;;
;;  contained in the SP0256-AL2.  This speech data is distributed with      ;;
;;  explicit permission from Microchip, Inc.  All such redistributions      ;;
;;  must retain this notice of copyright.                   ;;
;;                                      ;;
;;  No copyright claims are made on this data by the author(s) of SDK1600.  ;;
;;  Please see http://spatula-city.org/~im14u2c/sp0256-al2/ for details.    ;;
;;                                      ;;
;;==========================================================================;;

;* ======================================================================== *;
;*  These routines are placed into the public domain by their author.  All  *;
;*  copyright rights are hereby relinquished on the routines and data in    *;
;*  this file.  -- Joseph Zbiciak, 2008                     *;
;* ======================================================================== *;

;; ======================================================================== ;;
;;  INTELLIVOICE DRIVER ROUTINES                        ;;
;;  Written in 2002 by Joe Zbiciak <intvnut AT gmail.com>           ;;
;;  http://spatula-city.org/~im14u2c/intv/                  ;;
;; ======================================================================== ;;

;; ======================================================================== ;;
;;  GLOBAL VARIABLES USED BY THESE ROUTINES                 ;;
;;                                      ;;
;;  Note that some of these routines may use one or more global variables.  ;;
;;  If you use these routines, you will need to allocate the appropriate    ;;
;;  space in either 16-bit or 8-bit memory as appropriate.  Each global     ;;
;;  variable is listed with the routines which use it and the required      ;;
;;  memory width.                               ;;
;;                                      ;;
;;  Example declarations for these routines are shown below, commented out. ;;
;;  You should uncomment these and add them to your program to make use of  ;;
;;  the routine that needs them.  Make sure to assign these variables to    ;;
;;  locations that aren't used for anything else.               ;;
;; ======================================================================== ;;

            ; Used by       Req'd Width     Description
            ;-----------------------------------------------------
;IV.QH      EQU $110    ; IV_xxx    8-bit       Voice queue head
;IV.QT      EQU $111    ; IV_xxx    8-bit       Voice queue tail
;IV.Q       EQU $112    ; IV_xxx    8-bit       Voice queue  (8 bytes)
;IV.FLEN    EQU $11A    ; IV_xxx    8-bit       Length of FIFO data
;IV.FPTR    EQU $320    ; IV_xxx    16-bit      Current FIFO ptr.
;IV.PPTR    EQU $321    ; IV_xxx    16-bit      Current Phrase ptr.

;; ======================================================================== ;;
;;  MEMORY USAGE                                ;;
;;                                      ;;
;;  These routines implement a queue of "pending phrases" that will be      ;;
;;  played by the Intellivoice.  The user calls IV_PLAY to enqueue a    ;;
;;  phrase number.  Phrase numbers indicate either a RESROM sample or       ;;
;;  a compiled in phrase to be spoken.                      ;;
;;                                      ;;
;;  The user must compose an "IV_PHRASE_TBL", which is composed of      ;;
;;  pointers to phrases to be spoken.  Phrases are strings of pointers      ;;
;;  and RESROM triggers, terminated by a NUL.                   ;;
;;                                      ;;
;;  Phrase numbers 1 through 42 are RESROM samples.  Phrase numbers     ;;
;;  43 through 255 index into the IV_PHRASE_TBL.                ;;
;;                                      ;;
;;  SPECIAL NOTES                               ;;
;;                                      ;;
;;  Bit 7 of IV.QH and IV.QT is used to denote whether the Intellivoice     ;;
;;  is present.  If Intellivoice is present, this bit is clear.         ;;
;;                                      ;;
;;  Bit 6 of IV.QT is used to denote that we still need to do an ALD $00    ;;
;;  for FIFO'd voice data.                          ;;
;; ======================================================================== ;;


;; ======================================================================== ;;
;;  NAME                                    ;;
;;      IV_INIT     Initialize the Intellivoice                 ;;
;;                                      ;;
;;  AUTHOR                                  ;;
;;      Joseph Zbiciak <intvnut AT gmail.com>                   ;;
;;                                      ;;
;;  REVISION HISTORY                            ;;
;;      15-Sep-2002 Initial revision . . . . . . . . . . .  J. Zbiciak      ;;
;;                                      ;;
;;  INPUTS for IV_INIT                              ;;
;;      R5      Return address                          ;;
;;                                      ;;
;;  OUTPUTS                                 ;;
;;      R0      0 if Intellivoice found, -1 if not.             ;;
;;                                      ;;
;;  DESCRIPTION                                 ;;
;;      Resets Intellivoice, determines if it is actually there, and    ;;
;;      then initializes the IV structure.                  ;;
;; ------------------------------------------------------------------------ ;;
;;           Copyright (c) 2002, Joseph Zbiciak             ;;
;; ======================================================================== ;;

IV_INIT     PROC
        MVII    #$0400, R0      ;
        MVO     R0,     $0081       ; Reset the Intellivoice

        MVI     $0081,  R0      ; \
        RLC     R0,     2       ;  |-- See if we detect Intellivoice
        BOV     @@no_ivoice     ; /    once we've reset it.

        CLRR    R0          ;
        MVO     R0,     IV.FPTR     ; No data for FIFO
        MVO     R0,     IV.PPTR     ; No phrase being spoken
        MVO     R0,     IV.QH       ; Clear our queue
        MVO     R0,     IV.QT       ; Clear our queue
        JR      R5          ; Done!

@@no_ivoice:
        CLRR    R0
        MVO     R0,     IV.FPTR     ; No data for FIFO
        MVO     R0,     IV.PPTR     ; No phrase being spoken
        DECR    R0
        MVO     R0,     IV.QH       ; Set queue to -1 ("No Intellivoice")
        MVO     R0,     IV.QT       ; Set queue to -1 ("No Intellivoice")
;        JR      R5         ; Done!
        B       _wait           ; Special for IntyBASIC!
        ENDP

;; ======================================================================== ;;
;;  NAME                                    ;;
;;      IV_ISR      Interrupt service routine to feed Intellivoice      ;;
;;                                      ;;
;;  AUTHOR                                  ;;
;;      Joseph Zbiciak <intvnut AT gmail.com>                   ;;
;;                                      ;;
;;  REVISION HISTORY                            ;;
;;      15-Sep-2002 Initial revision . . . . . . . . . . .  J. Zbiciak      ;;
;;                                      ;;
;;  INPUTS for IV_ISR                               ;;
;;      R5      Return address                          ;;
;;                                      ;;
;;  OUTPUTS                                 ;;
;;      R0, R1, R4 trashed.                         ;;
;;                                      ;;
;;  NOTES                                   ;;
;;      Call this from your main interrupt service routine.         ;;
;; ------------------------------------------------------------------------ ;;
;;           Copyright (c) 2002, Joseph Zbiciak             ;;
;; ======================================================================== ;;
IV_ISR      PROC
        ;; ------------------------------------------------------------ ;;
        ;;  Check for Intellivoice.  Leave if none present.         ;;
        ;; ------------------------------------------------------------ ;;
        MVI     IV.QT,  R1      ; Get queue tail
        SWAP    R1,     2
        BPL     @@ok        ; Bit 7 set? If yes: No Intellivoice
@@ald_busy:
@@leave     JR      R5          ; Exit if no Intellivoice.


        ;; ------------------------------------------------------------ ;;
        ;;  Check to see if we pump samples into the FIFO.
        ;; ------------------------------------------------------------ ;;
@@ok:       MVI     IV.FPTR, R4     ; Get FIFO data pointer
        TSTR    R4          ; is it zero?
        BEQ     @@no_fifodata       ; Yes:  No data for FIFO.
@@fifo_fill:
        MVI     $0081,  R0      ; Read speech FIFO ready bit
        SLLC    R0,     1       ;
        BC      @@fifo_busy

        MVI@    R4,     R0      ; Get next word
        MVO     R0,     $0081       ; write it to the FIFO

        MVI     IV.FLEN, R0     ;\
        DECR    R0          ; |-- Decrement our FIFO'd data length
        MVO     R0,     IV.FLEN     ;/
        BEQ     @@last_fifo     ; If zero, we're done w/ FIFO
        MVO     R4,     IV.FPTR     ; Otherwise, save new pointer
        B       @@fifo_fill     ; ...and keep trying to load FIFO

@@last_fifo MVO     R0,     IV.FPTR     ; done with FIFO loading.
                    ; fall into ALD processing.


        ;; ------------------------------------------------------------ ;;
        ;;  Try to do an Address Load.  We do this in two settings:     ;;
        ;;   -- We have no FIFO data to load.               ;;
        ;;   -- We've loaded as much FIFO data as we can, but we    ;;
        ;;      might have an address load command to send for it.      ;;
        ;; ------------------------------------------------------------ ;;
@@fifo_busy:
@@no_fifodata:
        MVI     $0080,  R0      ; Read LRQ bit from ALD register
        SLLC    R0,     1
        BNC     @@ald_busy      ; LRQ is low, meaning we can't ALD.
                    ; So, leave.

        ;; ------------------------------------------------------------ ;;
        ;;  We can do an address load (ALD) on the SP0256.  Give FIFO   ;;
        ;;  driven ALDs priority, since we already started the FIFO     ;;
        ;;  load.  The "need ALD" bit is stored in bit 6 of IV.QT.      ;;
        ;; ------------------------------------------------------------ ;;
        ANDI    #$40,   R1      ; Is "Need FIFO ALD" bit set?
        BEQ     @@no_fifo_ald
        XOR     IV.QT,  R1      ;\__ Clear the "Need FIFO ALD" bit.
        MVO     R1,     IV.QT       ;/
        CLRR    R1
        MVO     R1,     $80     ; Load a 0 into ALD (trigger FIFO rd.)
        JR      R5          ; done!

        ;; ------------------------------------------------------------ ;;
        ;;  We don't need to ALD on behalf of the FIFO.  So, we grab    ;;
        ;;  the next thing off our phrase list.             ;;
        ;; ------------------------------------------------------------ ;;
@@no_fifo_ald:
        MVI     IV.PPTR, R4     ; Get phrase pointer.
        TSTR    R4          ; Is it zero?
        BEQ     @@next_phrase       ; Yes:  Get next phrase from queue.

        MVI@    R4,     R0
        TSTR    R0          ; Is it end of phrase?
        BNEQ    @@process_phrase    ; !=0:  Go do it.

        MVO     R0,     IV.PPTR     ;
@@next_phrase:
        MVI     IV.QT,  R1      ; reload queue tail (was trashed above)
        MOVR    R1,     R0      ; copy QT to R0 so we can increment it
        ANDI    #$7,    R1      ; Mask away flags in queue head
        CMP     IV.QH,  R1      ; Is it same as queue tail?
        BEQ     @@leave         ; Yes:  No more speech for now.

        INCR    R0
        ANDI    #$F7,   R0      ; mask away the possible 'carry'
        MVO     R0,     IV.QT       ; save updated queue tail

        ADDI    #IV.Q,  R1      ; Index into queue
        MVI@    R1,     R4      ; get next value from queue
        CMPI    #43,    R4      ; Is it a RESROM or Phrase?
        BNC     @@play_resrom_r4
@@new_phrase:
;        ADDI    #IV_PHRASE_TBL - 43, R4 ; Index into phrase table
;        MVI@    R4,     R4      ; Read from phrase table
        MVO     R4,     IV.PPTR
        JR      R5          ; we'll get to this phrase next time.

@@play_resrom_r4:
        MVO     R4,     $0080       ; Just ALD it
        JR      R5          ; and leave.

        ;; ------------------------------------------------------------ ;;
        ;;  We're in the middle of a phrase, so continue interpreting.  ;;
        ;; ------------------------------------------------------------ ;;
@@process_phrase:

        MVO     R4,     IV.PPTR     ; save new phrase pointer
        CMPI    #43,    R0      ; Is it a RESROM cue?
        BC      @@play_fifo     ; Just ALD it and leave.
@@play_resrom_r0
        MVO     R0,     $0080       ; Just ALD it
        JR      R5          ; and leave.
@@play_fifo:
        MVI     IV.FPTR,R1      ; Make sure not to stomp existing FIFO
        TSTR    R1          ; data.
        BEQ     @@new_fifo_ok
        DECR    R4          ; Oops, FIFO data still playing,
        MVO     R4,     IV.PPTR     ; so rewind.
        JR      R5          ; and leave.

@@new_fifo_ok:
        MOVR    R0,     R4      ;
        MVI@    R4,     R0      ; Get chunk length
        MVO     R0,     IV.FLEN     ; Init FIFO chunk length
        MVO     R4,     IV.FPTR     ; Init FIFO pointer
        MVI     IV.QT,  R0      ;\
        XORI    #$40,   R0      ; |- Set "Need ALD" bit in QT
        MVO     R0,     IV.QT       ;/

  IF 1      ; debug code        ;\
        ANDI    #$40,   R0      ; |   Debug code:  We should only
        BNEQ    @@qtok          ; |-- be here if "Need FIFO ALD"
        HLT     ;BUG!!          ; |   was already clear.
@@qtok                  ;/
  ENDI
        JR      R5          ; leave.

        ENDP


;; ======================================================================== ;;
;;  NAME                                    ;;
;;      IV_PLAY     Play a voice sample sequence.               ;;
;;                                      ;;
;;  AUTHOR                                  ;;
;;      Joseph Zbiciak <intvnut AT gmail.com>                   ;;
;;                                      ;;
;;  REVISION HISTORY                            ;;
;;      15-Sep-2002 Initial revision . . . . . . . . . . .  J. Zbiciak      ;;
;;                                      ;;
;;  INPUTS for IV_PLAY                              ;;
;;      R5      Invocation record, followed by return address.          ;;
;;          1 DECLE    Phrase number to play.               ;;
;;                                      ;;
;;  INPUTS for IV_PLAY.1                            ;;
;;      R0      Address of phrase to play.                  ;;
;;      R5      Return address                          ;;
;;                                      ;;
;;  OUTPUTS                                 ;;
;;      R0, R1  trashed                             ;;
;;      Z==0    if item not successfully queued.                ;;
;;      Z==1    if successfully queued.                     ;;
;;                                      ;;
;;  NOTES                                   ;;
;;      This code will drop phrases if the queue is full.           ;;
;;      Phrase numbers 1..42 are RESROM samples.  43..255 will index    ;;
;;      into the user-supplied IV_PHRASE_TBL.  43 will refer to the     ;;
;;      first entry, 44 to the second, and so on.  Phrase 0 is undefined.   ;;
;;                                      ;;
;; ------------------------------------------------------------------------ ;;
;;           Copyright (c) 2002, Joseph Zbiciak             ;;
;; ======================================================================== ;;
IV_PLAY     PROC
        MVI@    R5,     R0

@@1:    ; alternate entry point
        MVI     IV.QT,  R1      ; Get queue tail
        SWAP    R1,     2       ;\___ Leave if "no Intellivoice"
        BMI     @@leave         ;/    bit it set.
@@ok:
        DECR    R1          ;\
        ANDI    #$7,    R1      ; |-- See if we still have room
        CMP     IV.QH,  R1      ;/
        BEQ     @@leave         ; Leave if we're full

@@2:    MVI     IV.QH,  R1      ; Get our queue head pointer
        PSHR    R1          ;\
        INCR    R1          ; |
        ANDI    #$F7,   R1      ; |-- Increment it, removing
        MVO     R1,     IV.QH       ; |   carry but preserving flags.
        PULR    R1          ;/

        ADDI    #IV.Q,  R1      ;\__ Store phrase to queue
        MVO@    R0,     R1      ;/

@@leave:    JR      R5          ; Leave.
        ENDP

;; ======================================================================== ;;
;;  NAME                                    ;;
;;      IV_PLAYW    Play a voice sample sequence.  Wait for queue room.     ;;
;;                                      ;;
;;  AUTHOR                                  ;;
;;      Joseph Zbiciak <intvnut AT gmail.com>                   ;;
;;                                      ;;
;;  REVISION HISTORY                            ;;
;;      15-Sep-2002 Initial revision . . . . . . . . . . .  J. Zbiciak      ;;
;;                                      ;;
;;  INPUTS for IV_PLAY                              ;;
;;      R5      Invocation record, followed by return address.          ;;
;;          1 DECLE    Phrase number to play.               ;;
;;                                      ;;
;;  INPUTS for IV_PLAY.1                            ;;
;;      R0      Address of phrase to play.                  ;;
;;      R5      Return address                          ;;
;;                                      ;;
;;  OUTPUTS                                 ;;
;;      R0, R1  trashed                             ;;
;;                                      ;;
;;  NOTES                                   ;;
;;      This code will wait for a queue slot to open if queue is full.      ;;
;;      Phrase numbers 1..42 are RESROM samples.  43..255 will index    ;;
;;      into the user-supplied IV_PHRASE_TBL.  43 will refer to the     ;;
;;      first entry, 44 to the second, and so on.  Phrase 0 is undefined.   ;;
;;                                      ;;
;; ------------------------------------------------------------------------ ;;
;;           Copyright (c) 2002, Joseph Zbiciak             ;;
;; ======================================================================== ;;
IV_PLAYW    PROC
        MVI@    R5,     R0

@@1:    ; alternate entry point
        MVI     IV.QT,  R1      ; Get queue tail
        SWAP    R1,     2       ;\___ Leave if "no Intellivoice"
        BMI     IV_PLAY.leave       ;/    bit it set.
@@ok:
        DECR    R1          ;\
        ANDI    #$7,    R1      ; |-- See if we still have room
        CMP     IV.QH,  R1      ;/
        BEQ     @@1         ; wait for room
        B       IV_PLAY.2

        ENDP

;; ======================================================================== ;;
;;  NAME                                    ;;
;;      IV_HUSH     Flush the speech queue, and hush the Intellivoice.      ;;
;;                                      ;;
;;  AUTHOR                                  ;;
;;      Joseph Zbiciak <intvnut AT gmail.com>                   ;;
;;                                      ;;
;;  REVISION HISTORY                            ;;
;;      02-Feb-2018 Initial revision . . . . . . . . . . .  J. Zbiciak      ;;
;;                                      ;;
;;  INPUTS for IV_HUSH                              ;;
;;      None.                                   ;;
;;                                      ;;
;;  OUTPUTS                                 ;;
;;      R0 trashed.                             ;;
;;                                      ;;
;;  NOTES                                   ;;
;;      Returns via IV_WAIT.                        ;;
;;                                      ;;
;; ======================================================================== ;;
IV_HUSH:    PROC
        MVI     IV.QH,  R0
        SWAP    R0,     2
        BMI     IV_WAIT.leave

        DIS
        ;; We can't stop a phrase segment that's being FIFOed down.
        ;; We need to remember if we've committed to pushing ALD.
        ;; We _can_ stop new phrase segments from going down, and _can_
        ;; stop new phrases from being started.

        ;; Set head pointer to indicate we've inserted one item.
        MVI     IV.QH,  R0  ; Re-read, as an interrupt may have occurred
        ANDI    #$F0,   R0
        INCR    R0
        MVO     R0,     IV.QH

        ;; Reset tail pointer, keeping "need ALD" bit and other flags.
        MVI     IV.QT,  R0
        ANDI    #$F0,   R0
        MVO     R0,     IV.QT

        ;; Reset the phrase pointer, to stop a long phrase.
        CLRR    R0
        MVO     R0,     IV.PPTR

        ;; Queue a PA1 in the queue.  Since we're can't guarantee the user
        ;; has included resrom.asm, let's just use the raw number (5).
        MVII    #5,     R0
        MVO     R0,     IV.Q

        ;; Re-enable interrupts and wait for Intellivoice to shut up.
        ;;
        ;; We can't just jump to IV_WAIT.q_loop, as we need to reload
        ;; IV.QH into R0, and I'm really committed to only using R0.
;       JE      IV_WAIT
        EIS
        ; fallthrough into IV_WAIT
        ENDP

;; ======================================================================== ;;
;;  NAME                                    ;;
;;      IV_WAIT     Wait for voice queue to empty.              ;;
;;                                      ;;
;;  AUTHOR                                  ;;
;;      Joseph Zbiciak <intvnut AT gmail.com>                   ;;
;;                                      ;;
;;  REVISION HISTORY                            ;;
;;      15-Sep-2002 Initial revision . . . . . . . . . . .  J. Zbiciak      ;;
;;                                      ;;
;;  INPUTS for IV_WAIT                              ;;
;;      R5      Return address                          ;;
;;                                      ;;
;;  OUTPUTS                                 ;;
;;      R0      trashed.                            ;;
;;                                      ;;
;;  NOTES                                   ;;
;;      This waits until the Intellivoice is nearly completely quiescent.   ;;
;;      Some voice data may still be spoken from the last triggered     ;;
;;      phrase.  To truly wait for *that* to be spoken, speak a 'pause'     ;;
;;      (eg. RESROM.pa1) and then call IV_WAIT.                 ;;
;; ------------------------------------------------------------------------ ;;
;;           Copyright (c) 2002, Joseph Zbiciak             ;;
;; ======================================================================== ;;
IV_WAIT     PROC
        MVI     IV.QH,  R0
        CMPI    #$80, R0        ; test bit 7, leave if set.
        BC      @@leave

        ; Wait for queue to drain.
@@q_loop:   CMP     IV.QT,  R0
        BNEQ    @@q_loop

        ; Wait for FIFO and LRQ to say ready.
@@s_loop:   MVI     $81,    R0      ; Read FIFO status.  0 == ready.
        COMR    R0
        AND     $80,    R0      ; Merge w/ ALD status.  1 == ready
        TSTR    R0
        BPL     @@s_loop        ; if bit 15 == 0, not ready.

@@leave:    JR      R5
        ENDP

;; ======================================================================== ;;
;;  End of File:  ivoice.asm                        ;;
;; ======================================================================== ;;

;* ======================================================================== *;
;*  These routines are placed into the public domain by their author.  All  *;
;*  copyright rights are hereby relinquished on the routines and data in    *;
;*  this file.  -- Joseph Zbiciak, 2008                     *;
;* ======================================================================== *;

;; ======================================================================== ;;
;;  NAME                                    ;;
;;      IV_SAYNUM16 Say a 16-bit unsigned number using RESROM digits    ;;
;;                                      ;;
;;  AUTHOR                                  ;;
;;      Joseph Zbiciak <intvnut AT gmail.com>                   ;;
;;                                      ;;
;;  REVISION HISTORY                            ;;
;;      16-Sep-2002 Initial revision . . . . . . . . . . .  J. Zbiciak      ;;
;;                                      ;;
;;  INPUTS for IV_SAYNUM16                          ;;
;;      R0      Number to "speak"                       ;;
;;      R5      Return address                          ;;
;;                                      ;;
;;  OUTPUTS                                 ;;
;;                                      ;;
;;  DESCRIPTION                                 ;;
;;      "Says" a 16-bit number using IV_PLAYW to queue up the phrase.       ;;
;;      Because the number may be built from several segments, it could     ;;
;;      easily eat up the queue.  I believe the longest number will take    ;;
;;      7 queue entries -- that is, fill the queue.  Thus, this code    ;;
;;      could block, waiting for slots in the queue.            ;;
;; ======================================================================== ;;

IV_SAYNUM16 PROC
        PSHR    R5

        TSTR    R0
        BEQ     @@zero      ; Special case:  Just say "zero"

        ;; ------------------------------------------------------------ ;;
        ;;  First, try to pull off 'thousands'.  We call ourselves      ;;
        ;;  recursively to play the the number of thousands.        ;;
        ;; ------------------------------------------------------------ ;;
        CLRR    R1
@@thloop:   INCR    R1
        SUBI    #1000,  R0
        BC      @@thloop

        ADDI    #1000,  R0
        PSHR    R0
        DECR    R1
        BEQ     @@no_thousand

        CALL    IV_SAYNUM16.recurse

        CALL    IV_PLAYW
        DECLE   36  ; THOUSAND

@@no_thousand
        PULR    R1

        ;; ------------------------------------------------------------ ;;
        ;;  Now try to play hundreds.                   ;;
        ;; ------------------------------------------------------------ ;;
        MVII    #7-1, R0    ; ZERO
        CMPI    #100,   R1
        BNC     @@no_hundred

@@hloop:    INCR    R0
        SUBI    #100,   R1
        BC      @@hloop
        ADDI    #100,   R1

        PSHR    R1

        CALL    IV_PLAYW.1

        CALL    IV_PLAYW
        DECLE   35  ; HUNDRED

        PULR    R1
        B       @@notrecurse    ; skip "PSHR R5"
@@recurse:  PSHR    R5          ; recursive entry point for 'thousand'

@@no_hundred:
@@notrecurse:
        MOVR    R1,     R0
        BEQ     @@leave

        SUBI    #20,    R1
        BNC     @@teens

        MVII    #27-1, R0   ; TWENTY
@@tyloop    INCR    R0
        SUBI    #10,    R1
        BC      @@tyloop
        ADDI    #10,    R1

        PSHR    R1
        CALL    IV_PLAYW.1

        PULR    R0
        TSTR    R0
        BEQ     @@leave

@@teens:
@@zero:     ADDI    #7, R0  ; ZERO

        CALL    IV_PLAYW.1

@@leave     PULR    PC
        ENDP

;; ======================================================================== ;;
;;  End of File:  saynum16.asm                          ;;
;; ======================================================================== ;;

IV_INIT_and_wait:     EQU IV_INIT

    ELSE

IV_INIT_and_wait:     EQU _wait    ; No voice init; just WAIT.

    ENDI

    IF DEFINED intybasic_flash

;; ======================================================================== ;;
;;  JLP "Save Game" support                         ;;
;; ======================================================================== ;;
JF.first    EQU     $8023
JF.last     EQU     $8024
JF.addr     EQU     $8025
JF.row      EQU     $8026

JF.wrcmd    EQU     $802D
JF.rdcmd    EQU     $802E
JF.ercmd    EQU     $802F
JF.wrkey    EQU     $C0DE
JF.rdkey    EQU     $DEC0
JF.erkey    EQU     $BEEF

JF.write:   DECLE   JF.wrcmd,   JF.wrkey    ; Copy JLP RAM to flash row
JF.read:    DECLE   JF.rdcmd,   JF.rdkey    ; Copy flash row to JLP RAM
JF.erase:   DECLE   JF.ercmd,   JF.erkey    ; Erase flash sector

;; ======================================================================== ;;
;;  JF.INIT     Copy JLP save-game support routine to System RAM    ;;
;; ======================================================================== ;;
JF.INIT     PROC
        PSHR    R5
        MVII    #@@__code,  R5
        MVII    #JF.SYSRAM, R4
        REPEAT  5
        MVI@    R5,     R0      ; \_ Copy code fragment to System RAM
        MVO@    R0,     R4      ; /
        ENDR
        PULR    PC

        ;; === start of code that will run from RAM
@@__code:   MVO@    R0,     R1      ; JF.SYSRAM + 0: initiate command
        ADD@    R1,     PC      ; JF.SYSRAM + 1: Wait for JLP to return
        JR      R5          ; JF.SYSRAM + 2:
        MVO@    R2,     R2      ; JF.SYSRAM + 3: \__ simple ISR
        JR      R5          ; JF.SYSRAM + 4: /
        ;; === end of code that will run from RAM
        ENDP

;; ======================================================================== ;;
;;  JF.CMD      Issue a JLP Flash command                   ;;
;;                                      ;;
;;  INPUT                                   ;;
;;      R0  Slot number to operate on                       ;;
;;      R1  Address to copy to/from in JLP RAM                  ;;
;;      @R5 Command to invoke:                          ;;
;;                                      ;;
;;          JF.write -- Copy JLP RAM to Flash               ;;
;;          JF.read  -- Copy Flash to JLP RAM               ;;
;;          JF.erase -- Erase flash sector                  ;;
;;                                      ;;
;;  OUTPUT                                  ;;
;;      R0 - R4 not modified.  (Saved and restored across call)         ;;
;;      JLP command executed                        ;;
;;                                      ;;
;;  NOTES                                   ;;
;;      This code requires two short routines in the console's System RAM.  ;;
;;      It also requires that the system stack reside in System RAM.    ;;
;;      Because an interrupt may occur during the code's execution, there   ;;
;;      must be sufficient stack space to service the interrupt (8 words).  ;;
;;                                      ;;
;;      The code also relies on the fact that the EXEC ISR dispatch does    ;;
;;      not modify R2.  This allows us to initialize R2 for the ISR ahead   ;;
;;      of time, rather than in the ISR.                    ;;
;; ======================================================================== ;;
JF.CMD      PROC

        MVO     R4,     JF.SV.R4    ; \
        MVII    #JF.SV.R0,  R4      ;  |
        MVO@    R0,     R4      ;  |- Save registers, but not on
        MVO@    R1,     R4      ;  |  the stack.  (limit stack use)
        MVO@    R2,     R4      ; /

        MVI@    R5,     R4      ; Get command to invoke

        MVO     R5,     JF.SV.R5    ; save return address

        DIS
        MVO     R1,     JF.addr     ; \_ Save SG arguments in JLP
        MVO     R0,     JF.row      ; /

        MVI@    R4,     R1      ; Get command address
        MVI@    R4,     R0      ; Get unlock word

        MVII    #$100,      R4      ; \
        SDBD                ;  |_ Save old ISR in save area
        MVI@    R4,     R2      ;  |
        MVO     R2,     JF.SV.ISR   ; /

        MVII    #JF.SYSRAM + 3, R2      ; \
        MVO     R2,     $100    ;  |_ Set up new ISR in RAM
        SWAP    R2              ;  |
        MVO     R2,     $101    ; /

        MVII    #$20,       R2      ; Address of STIC handshake
        JSRE    R5,  JF.SYSRAM      ; Invoke the command

        MVI     JF.SV.ISR,  R2      ; \
        MVO     R2,     $100    ;  |_ Restore old ISR
        SWAP    R2              ;  |
        MVO     R2,     $101    ; /

        MVII    #JF.SV.R0,  R5      ; \
        MVI@    R5,     R0      ;  |
        MVI@    R5,     R1      ;  |- Restore registers
        MVI@    R5,     R2      ;  |
        MVI@    R5,     R4      ; /
        MVI@    R5,     PC      ; Return

        ENDP


    ENDI

    IF DEFINED intybasic_fastmult

; Quarter Square Multiplication
; Assembly code by Joe Zbiciak, 2015
; Released to public domain.

QSQR8_TBL:  PROC
        DECLE   $3F80, $3F01, $3E82, $3E04, $3D86, $3D09, $3C8C, $3C10
        DECLE   $3B94, $3B19, $3A9E, $3A24, $39AA, $3931, $38B8, $3840
        DECLE   $37C8, $3751, $36DA, $3664, $35EE, $3579, $3504, $3490
        DECLE   $341C, $33A9, $3336, $32C4, $3252, $31E1, $3170, $3100
        DECLE   $3090, $3021, $2FB2, $2F44, $2ED6, $2E69, $2DFC, $2D90
        DECLE   $2D24, $2CB9, $2C4E, $2BE4, $2B7A, $2B11, $2AA8, $2A40
        DECLE   $29D8, $2971, $290A, $28A4, $283E, $27D9, $2774, $2710
        DECLE   $26AC, $2649, $25E6, $2584, $2522, $24C1, $2460, $2400
        DECLE   $23A0, $2341, $22E2, $2284, $2226, $21C9, $216C, $2110
        DECLE   $20B4, $2059, $1FFE, $1FA4, $1F4A, $1EF1, $1E98, $1E40
        DECLE   $1DE8, $1D91, $1D3A, $1CE4, $1C8E, $1C39, $1BE4, $1B90
        DECLE   $1B3C, $1AE9, $1A96, $1A44, $19F2, $19A1, $1950, $1900
        DECLE   $18B0, $1861, $1812, $17C4, $1776, $1729, $16DC, $1690
        DECLE   $1644, $15F9, $15AE, $1564, $151A, $14D1, $1488, $1440
        DECLE   $13F8, $13B1, $136A, $1324, $12DE, $1299, $1254, $1210
        DECLE   $11CC, $1189, $1146, $1104, $10C2, $1081, $1040, $1000
        DECLE   $0FC0, $0F81, $0F42, $0F04, $0EC6, $0E89, $0E4C, $0E10
        DECLE   $0DD4, $0D99, $0D5E, $0D24, $0CEA, $0CB1, $0C78, $0C40
        DECLE   $0C08, $0BD1, $0B9A, $0B64, $0B2E, $0AF9, $0AC4, $0A90
        DECLE   $0A5C, $0A29, $09F6, $09C4, $0992, $0961, $0930, $0900
        DECLE   $08D0, $08A1, $0872, $0844, $0816, $07E9, $07BC, $0790
        DECLE   $0764, $0739, $070E, $06E4, $06BA, $0691, $0668, $0640
        DECLE   $0618, $05F1, $05CA, $05A4, $057E, $0559, $0534, $0510
        DECLE   $04EC, $04C9, $04A6, $0484, $0462, $0441, $0420, $0400
        DECLE   $03E0, $03C1, $03A2, $0384, $0366, $0349, $032C, $0310
        DECLE   $02F4, $02D9, $02BE, $02A4, $028A, $0271, $0258, $0240
        DECLE   $0228, $0211, $01FA, $01E4, $01CE, $01B9, $01A4, $0190
        DECLE   $017C, $0169, $0156, $0144, $0132, $0121, $0110, $0100
        DECLE   $00F0, $00E1, $00D2, $00C4, $00B6, $00A9, $009C, $0090
        DECLE   $0084, $0079, $006E, $0064, $005A, $0051, $0048, $0040
        DECLE   $0038, $0031, $002A, $0024, $001E, $0019, $0014, $0010
        DECLE   $000C, $0009, $0006, $0004, $0002, $0001, $0000
@@mid:
        DECLE   $0000, $0000, $0001, $0002, $0004, $0006, $0009, $000C
        DECLE   $0010, $0014, $0019, $001E, $0024, $002A, $0031, $0038
        DECLE   $0040, $0048, $0051, $005A, $0064, $006E, $0079, $0084
        DECLE   $0090, $009C, $00A9, $00B6, $00C4, $00D2, $00E1, $00F0
        DECLE   $0100, $0110, $0121, $0132, $0144, $0156, $0169, $017C
        DECLE   $0190, $01A4, $01B9, $01CE, $01E4, $01FA, $0211, $0228
        DECLE   $0240, $0258, $0271, $028A, $02A4, $02BE, $02D9, $02F4
        DECLE   $0310, $032C, $0349, $0366, $0384, $03A2, $03C1, $03E0
        DECLE   $0400, $0420, $0441, $0462, $0484, $04A6, $04C9, $04EC
        DECLE   $0510, $0534, $0559, $057E, $05A4, $05CA, $05F1, $0618
        DECLE   $0640, $0668, $0691, $06BA, $06E4, $070E, $0739, $0764
        DECLE   $0790, $07BC, $07E9, $0816, $0844, $0872, $08A1, $08D0
        DECLE   $0900, $0930, $0961, $0992, $09C4, $09F6, $0A29, $0A5C
        DECLE   $0A90, $0AC4, $0AF9, $0B2E, $0B64, $0B9A, $0BD1, $0C08
        DECLE   $0C40, $0C78, $0CB1, $0CEA, $0D24, $0D5E, $0D99, $0DD4
        DECLE   $0E10, $0E4C, $0E89, $0EC6, $0F04, $0F42, $0F81, $0FC0
        DECLE   $1000, $1040, $1081, $10C2, $1104, $1146, $1189, $11CC
        DECLE   $1210, $1254, $1299, $12DE, $1324, $136A, $13B1, $13F8
        DECLE   $1440, $1488, $14D1, $151A, $1564, $15AE, $15F9, $1644
        DECLE   $1690, $16DC, $1729, $1776, $17C4, $1812, $1861, $18B0
        DECLE   $1900, $1950, $19A1, $19F2, $1A44, $1A96, $1AE9, $1B3C
        DECLE   $1B90, $1BE4, $1C39, $1C8E, $1CE4, $1D3A, $1D91, $1DE8
        DECLE   $1E40, $1E98, $1EF1, $1F4A, $1FA4, $1FFE, $2059, $20B4
        DECLE   $2110, $216C, $21C9, $2226, $2284, $22E2, $2341, $23A0
        DECLE   $2400, $2460, $24C1, $2522, $2584, $25E6, $2649, $26AC
        DECLE   $2710, $2774, $27D9, $283E, $28A4, $290A, $2971, $29D8
        DECLE   $2A40, $2AA8, $2B11, $2B7A, $2BE4, $2C4E, $2CB9, $2D24
        DECLE   $2D90, $2DFC, $2E69, $2ED6, $2F44, $2FB2, $3021, $3090
        DECLE   $3100, $3170, $31E1, $3252, $32C4, $3336, $33A9, $341C
        DECLE   $3490, $3504, $3579, $35EE, $3664, $36DA, $3751, $37C8
        DECLE   $3840, $38B8, $3931, $39AA, $3A24, $3A9E, $3B19, $3B94
        DECLE   $3C10, $3C8C, $3D09, $3D86, $3E04, $3E82, $3F01, $3F80
        DECLE   $4000, $4080, $4101, $4182, $4204, $4286, $4309, $438C
        DECLE   $4410, $4494, $4519, $459E, $4624, $46AA, $4731, $47B8
        DECLE   $4840, $48C8, $4951, $49DA, $4A64, $4AEE, $4B79, $4C04
        DECLE   $4C90, $4D1C, $4DA9, $4E36, $4EC4, $4F52, $4FE1, $5070
        DECLE   $5100, $5190, $5221, $52B2, $5344, $53D6, $5469, $54FC
        DECLE   $5590, $5624, $56B9, $574E, $57E4, $587A, $5911, $59A8
        DECLE   $5A40, $5AD8, $5B71, $5C0A, $5CA4, $5D3E, $5DD9, $5E74
        DECLE   $5F10, $5FAC, $6049, $60E6, $6184, $6222, $62C1, $6360
        DECLE   $6400, $64A0, $6541, $65E2, $6684, $6726, $67C9, $686C
        DECLE   $6910, $69B4, $6A59, $6AFE, $6BA4, $6C4A, $6CF1, $6D98
        DECLE   $6E40, $6EE8, $6F91, $703A, $70E4, $718E, $7239, $72E4
        DECLE   $7390, $743C, $74E9, $7596, $7644, $76F2, $77A1, $7850
        DECLE   $7900, $79B0, $7A61, $7B12, $7BC4, $7C76, $7D29, $7DDC
        DECLE   $7E90, $7F44, $7FF9, $80AE, $8164, $821A, $82D1, $8388
        DECLE   $8440, $84F8, $85B1, $866A, $8724, $87DE, $8899, $8954
        DECLE   $8A10, $8ACC, $8B89, $8C46, $8D04, $8DC2, $8E81, $8F40
        DECLE   $9000, $90C0, $9181, $9242, $9304, $93C6, $9489, $954C
        DECLE   $9610, $96D4, $9799, $985E, $9924, $99EA, $9AB1, $9B78
        DECLE   $9C40, $9D08, $9DD1, $9E9A, $9F64, $A02E, $A0F9, $A1C4
        DECLE   $A290, $A35C, $A429, $A4F6, $A5C4, $A692, $A761, $A830
        DECLE   $A900, $A9D0, $AAA1, $AB72, $AC44, $AD16, $ADE9, $AEBC
        DECLE   $AF90, $B064, $B139, $B20E, $B2E4, $B3BA, $B491, $B568
        DECLE   $B640, $B718, $B7F1, $B8CA, $B9A4, $BA7E, $BB59, $BC34
        DECLE   $BD10, $BDEC, $BEC9, $BFA6, $C084, $C162, $C241, $C320
        DECLE   $C400, $C4E0, $C5C1, $C6A2, $C784, $C866, $C949, $CA2C
        DECLE   $CB10, $CBF4, $CCD9, $CDBE, $CEA4, $CF8A, $D071, $D158
        DECLE   $D240, $D328, $D411, $D4FA, $D5E4, $D6CE, $D7B9, $D8A4
        DECLE   $D990, $DA7C, $DB69, $DC56, $DD44, $DE32, $DF21, $E010
        DECLE   $E100, $E1F0, $E2E1, $E3D2, $E4C4, $E5B6, $E6A9, $E79C
        DECLE   $E890, $E984, $EA79, $EB6E, $EC64, $ED5A, $EE51, $EF48
        DECLE   $F040, $F138, $F231, $F32A, $F424, $F51E, $F619, $F714
        DECLE   $F810, $F90C, $FA09, $FB06, $FC04, $FD02, $FE01
        ENDP

; R0 = R0 * R1, where R0 and R1 are unsigned 8-bit values
; Destroys R1, R4
qs_mpy8:    PROC
        MOVR    R0,         R4      ;   6
        ADDI    #QSQR8_TBL.mid, R1      ;   8
        ADDR    R1,         R4      ;   6   a + b
        SUBR    R0,         R1      ;   6   a - b
@@ok:       MVI@    R4,         R0      ;   8
        SUB@    R1,         R0      ;   8
        JR      R5              ;   7
                        ;----
                        ;  49
        ENDP


; R1 = R0 * R1, where R0 and R1 are 16-bit values
; destroys R0, R2, R3, R4, R5
qs_mpy16:   PROC
        PSHR    R5          ;   9

        ; Unpack lo/hi
        MOVR    R0,     R2      ;   6
        ANDI    #$FF,       R0      ;   8   R0 is lo(a)
        XORR    R0,     R2      ;   6
        SWAP    R2          ;   6   R2 is hi(a)

        MOVR    R1,     R3      ;   6   R3 is orig 16-bit b
        ANDI    #$FF,       R1      ;   8   R1 is lo(b)
        MOVR    R1,     R5      ;   6   R5 is lo(b)
        XORR    R1,     R3      ;   6
        SWAP    R3          ;   6   R3 is hi(b)
                    ;----
                    ;  67

        ; lo * lo
        MOVR    R0,     R4      ;   6   R4 is lo(a)
        ADDI    #QSQR8_TBL.mid, R1  ;   8
        ADDR    R1,     R4      ;   6   R4 = lo(a) + lo(b)
        SUBR    R0,     R1      ;   6   R1 = lo(a) - lo(b)

@@pos_ll:   MVI@    R4,     R4      ;   8   R4 = qstbl[lo(a)+lo(b)]
        SUB@    R1,     R4      ;   8   R4 = lo(a)*lo(b)
                    ;----
                    ;  42
                    ;  67 (carried forward)
                    ;----
                    ; 109

        ; lo * hi
        MOVR    R0,     R1      ;   6   R0 = R1 = lo(a)
        ADDI    #QSQR8_TBL.mid, R3  ;   8
        ADDR    R3,     R1      ;   6   R1 = hi(b) + lo(a)
        SUBR    R0,     R3      ;   6   R3 = hi(b) - lo(a)

@@pos_lh:   MVI@    R1,     R1      ;   8   R1 = qstbl[hi(b)-lo(a)]
        SUB@    R3,     R1      ;   8   R1 = lo(a)*hi(b)
                    ;----
                    ;  42
                    ; 109 (carried forward)
                    ;----
                    ; 151

        ; hi * lo
        MOVR    R5,     R0      ;   6   R5 = R0 = lo(b)
        ADDI    #QSQR8_TBL.mid, R2  ;   8
        ADDR    R2,     R5      ;   6   R3 = hi(a) + lo(b)
        SUBR    R0,     R2      ;   6   R2 = hi(a) - lo(b)

@@pos_hl:   ADD@    R5,     R1      ;   8   \_ R1 = lo(a)*hi(b)+hi(a)*lo(b)
        SUB@    R2,     R1      ;   8   /
                    ;----
                    ;  42
                    ; 151 (carried forward)
                    ;----
                    ; 193

        SWAP    R1          ;   6   \_ shift upper product left 8
        ANDI    #$FF00,     R1      ;   8   /
        ADDR    R4,     R1      ;   6   final product
        PULR    PC          ;  12
                    ;----
                    ;  32
                    ; 193 (carried forward)
                    ;----
                    ; 225
        ENDP

    ENDI

    IF DEFINED intybasic_fastdiv

; Fast unsigned division/remainder
; Assembly code by Oscar Toledo G. Jul/10/2015
; Released to public domain.

    ; Ultrafast unsigned division/remainder operation
    ; Entry: R0 = Dividend
    ;    R1 = Divisor
    ; Output: R0 = Quotient
    ;     R2 = Remainder
    ; Worst case: 6 + 6 + 9 + 496 = 517 cycles
    ; Best case: 6 + (6 + 7) * 16 = 214 cycles

uf_udiv16:    PROC
    CLRR R2        ; 6
    SLLC R0,1    ; 6
    BC @@1        ; 7/9
    SLLC R0,1    ; 6
    BC @@2        ; 7/9
    SLLC R0,1    ; 6
    BC @@3        ; 7/9
    SLLC R0,1    ; 6
    BC @@4        ; 7/9
    SLLC R0,1    ; 6
    BC @@5        ; 7/9
    SLLC R0,1    ; 6
    BC @@6        ; 7/9
    SLLC R0,1    ; 6
    BC @@7        ; 7/9
    SLLC R0,1    ; 6
    BC @@8        ; 7/9
    SLLC R0,1    ; 6
    BC @@9        ; 7/9
    SLLC R0,1    ; 6
    BC @@10        ; 7/9
    SLLC R0,1    ; 6
    BC @@11        ; 7/9
    SLLC R0,1    ; 6
    BC @@12        ; 7/9
    SLLC R0,1    ; 6
    BC @@13        ; 7/9
    SLLC R0,1    ; 6
    BC @@14        ; 7/9
    SLLC R0,1    ; 6
    BC @@15        ; 7/9
    SLLC R0,1    ; 6
    BC @@16        ; 7/9
    JR R5

@@1:    RLC R2,1    ; 6
    CMPR R1,R2    ; 6
    BNC $+3        ; 7/9
    SUBR R1,R2    ; 6
    RLC R0,1    ; 6
@@2:    RLC R2,1    ; 6
    CMPR R1,R2    ; 6
    BNC $+3        ; 7/9
    SUBR R1,R2    ; 6
    RLC R0,1    ; 6
@@3:    RLC R2,1    ; 6
    CMPR R1,R2    ; 6
    BNC $+3        ; 7/9
    SUBR R1,R2    ; 6
    RLC R0,1    ; 6
@@4:    RLC R2,1    ; 6
    CMPR R1,R2    ; 6
    BNC $+3        ; 7/9
    SUBR R1,R2    ; 6
    RLC R0,1    ; 6
@@5:    RLC R2,1    ; 6
    CMPR R1,R2    ; 6
    BNC $+3        ; 7/9
    SUBR R1,R2    ; 6
    RLC R0,1    ; 6
@@6:    RLC R2,1    ; 6
    CMPR R1,R2    ; 6
    BNC $+3        ; 7/9
    SUBR R1,R2    ; 6
    RLC R0,1    ; 6
@@7:    RLC R2,1    ; 6
    CMPR R1,R2    ; 6
    BNC $+3        ; 7/9
    SUBR R1,R2    ; 6
    RLC R0,1    ; 6
@@8:    RLC R2,1    ; 6
    CMPR R1,R2    ; 6
    BNC $+3        ; 7/9
    SUBR R1,R2    ; 6
    RLC R0,1    ; 6
@@9:    RLC R2,1    ; 6
    CMPR R1,R2    ; 6
    BNC $+3        ; 7/9
    SUBR R1,R2    ; 6
    RLC R0,1    ; 6
@@10:    RLC R2,1    ; 6
    CMPR R1,R2    ; 6
    BNC $+3        ; 7/9
    SUBR R1,R2    ; 6
    RLC R0,1    ; 6
@@11:    RLC R2,1    ; 6
    CMPR R1,R2    ; 6
    BNC $+3        ; 7/9
    SUBR R1,R2    ; 6
    RLC R0,1    ; 6
@@12:    RLC R2,1    ; 6
    CMPR R1,R2    ; 6
    BNC $+3        ; 7/9
    SUBR R1,R2    ; 6
    RLC R0,1    ; 6
@@13:    RLC R2,1    ; 6
    CMPR R1,R2    ; 6
    BNC $+3        ; 7/9
    SUBR R1,R2    ; 6
    RLC R0,1    ; 6
@@14:    RLC R2,1    ; 6
    CMPR R1,R2    ; 6
    BNC $+3        ; 7/9
    SUBR R1,R2    ; 6
    RLC R0,1    ; 6
@@15:    RLC R2,1    ; 6
    CMPR R1,R2    ; 6
    BNC $+3        ; 7/9
    SUBR R1,R2    ; 6
    RLC R0,1    ; 6
@@16:    RLC R2,1    ; 6
    CMPR R1,R2    ; 6
    BNC $+3        ; 7/9
    SUBR R1,R2    ; 6
    RLC R0,1    ; 6
    JR R5

    ENDP

    ENDI

    IF DEFINED intybasic_ecs
    ORG $4800    ; Available up to $4FFF

    ; Disable ECS ROMs so that they don't conflict with us
    MVII    #$2A5F, R0
    MVO     R0,     $2FFF
    MVII    #$7A5F, R0
    MVO     R0,     $7FFF
    MVII    #$EA5F, R0
    MVO     R0,     $EFFF

    B       $1041       ; resume boot

    ENDI

    ORG $200,$200,"-RWB"

Q2:    ; Reserved label for #BACKTAB

    ORG $319,$319,"-RWB"
    ;
    ; 16-bits variables
    ; Note IntyBASIC variables grow up starting in $308.
    ;
    IF DEFINED intybasic_voice
IV.Q:      RMB 8    ; IV_xxx    16-bit      Voice queue  (8 words)
IV.FPTR:   RMB 1    ; IV_xxx    16-bit      Current FIFO ptr.
IV.PPTR:   RMB 1    ; IV_xxx    16-bit      Current Phrase ptr.
    ENDI

    ORG $323,$323,"-RWB"

_scroll_buffer: RMB 20  ; Sometimes this is unused
_music_gosub:    RMB 1    ; GOSUB pointer
_music_table:    RMB 1    ; Note table
_music_p:    RMB 1    ; Pointer to music
_frame:     RMB 1   ; Current frame
_read:      RMB 1   ; Pointer to DATA
_gram_bitmap:   RMB 1   ; Bitmap for definition
_gram2_bitmap:  RMB 1   ; Secondary bitmap for definition
_screen:    RMB 1       ; Pointer to current screen position
_color:     RMB 1       ; Current color

_col0:      RMB 1       ; Collision status for MOB0
_col1:      RMB 1       ; Collision status for MOB1
_col2:      RMB 1       ; Collision status for MOB2
_col3:      RMB 1       ; Collision status for MOB3
_col4:      RMB 1       ; Collision status for MOB4
_col5:      RMB 1       ; Collision status for MOB5
_col6:      RMB 1       ; Collision status for MOB6
_col7:      RMB 1       ; Collision status for MOB7

Q1:            ; Reserved label for #MOBSHADOW
_mobs:      RMB 3*8     ; MOB buffer

SCRATCH:    ORG $100,$100,"-RWBN"
    ;
    ; 8-bits variables
    ;
ISRVEC:     RMB 2       ; Pointer to ISR vector (required by Intellivision ROM)
_int:       RMB 1       ; Signals interrupt received
_ntsc:      RMB 1       ; bit 0 = 1=NTSC, 0=PAL. Bit 1 = 1=ECS detected.
_rand:      RMB 1       ; Pseudo-random value
_gram_target:   RMB 1   ; Contains GRAM card number
_gram_total:    RMB 1   ; Contains total GRAM cards for definition
_gram2_target:  RMB 1   ; Contains GRAM card number
_gram2_total:   RMB 1   ; Contains total GRAM cards for definition
_mode_select:   RMB 1   ; Graphics mode selection
_border_color:  RMB 1   ; Border color
_border_mask:   RMB 1   ; Border mask
    IF DEFINED intybasic_keypad
_cnt1_p0:   RMB 1       ; Debouncing 1
_cnt1_p1:   RMB 1       ; Debouncing 2
_cnt1_key:  RMB 1       ; Currently pressed key
_cnt2_p0:   RMB 1       ; Debouncing 1
_cnt2_p1:   RMB 1       ; Debouncing 2
_cnt2_key:  RMB 1       ; Currently pressed key
    ENDI
    IF DEFINED intybasic_scroll
_scroll_x:  RMB 1       ; Scroll X offset
_scroll_y:  RMB 1       ; Scroll Y offset
_scroll_d:  RMB 1       ; Scroll direction
    ENDI
    IF DEFINED intybasic_music
_music_start:    RMB 2    ; Start of music

_music_mode: RMB 1      ; Music mode (0= Not using PSG, 2= Simple, 4= Full, add 1 if using noise channel for drums)
_music_frame: RMB 1     ; Music frame (for 50 hz fixed)
_music_tc:  RMB 1       ; Time counter
_music_t:   RMB 1       ; Time base
_music_i1:  RMB 1       ; Instrument 1
_music_s1:  RMB 1       ; Sample pointer 1
_music_n1:  RMB 1       ; Note 1
_music_i2:  RMB 1       ; Instrument 2
_music_s2:  RMB 1       ; Sample pointer 2
_music_n2:  RMB 1       ; Note 2
_music_i3:  RMB 1       ; Instrument 3
_music_s3:  RMB 1       ; Sample pointer 3
_music_n3:  RMB 1       ; Note 3
_music_s4:  RMB 1       ; Sample pointer 4
_music_n4:  RMB 1       ; Note 4 (really it's drum)

_music_freq10:    RMB 1   ; Low byte frequency A
_music_freq20:    RMB 1   ; Low byte frequency B
_music_freq30:    RMB 1   ; Low byte frequency C
_music_freq11:    RMB 1   ; High byte frequency A
_music_freq21:    RMB 1   ; High byte frequency B
_music_freq31:    RMB 1   ; High byte frequency C
_music_mix:    RMB 1   ; Mixer
_music_noise:    RMB 1   ; Noise
_music_vol1:    RMB 1   ; Volume A
_music_vol2:    RMB 1   ; Volume B
_music_vol3:    RMB 1   ; Volume C
    ENDI
    IF DEFINED intybasic_music_ecs
_music_i5:  RMB 1       ; Instrument 5
_music_s5:  RMB 1       ; Sample pointer 5
_music_n5:  RMB 1       ; Note 5
_music_i6:  RMB 1       ; Instrument 6
_music_s6:  RMB 1       ; Sample pointer 6
_music_n6:  RMB 1       ; Note 6
_music_i7:  RMB 1       ; Instrument 7
_music_s7:  RMB 1       ; Sample pointer 7
_music_n7:  RMB 1       ; Note 7
_music_s8:  RMB 1       ; Sample pointer 8
_music_n8:  RMB 1       ; Note 8 (really it's drum)

_music2_freq10:    RMB 1   ; Low byte frequency A
_music2_freq20:    RMB 1   ; Low byte frequency B
_music2_freq30:    RMB 1   ; Low byte frequency C
_music2_freq11:    RMB 1   ; High byte frequency A
_music2_freq21:    RMB 1   ; High byte frequency B
_music2_freq31:    RMB 1   ; High byte frequency C
_music2_mix:    RMB 1   ; Mixer
_music2_noise:    RMB 1   ; Noise
_music2_vol1:    RMB 1   ; Volume A
_music2_vol2:    RMB 1   ; Volume B
_music2_vol3:    RMB 1   ; Volume C
    ENDI
    IF DEFINED intybasic_music_volume
_music_vol:    RMB 1    ; Global music volume
    ENDI
    IF DEFINED intybasic_voice
IV.QH:     RMB 1    ; IV_xxx    8-bit       Voice queue head
IV.QT:     RMB 1    ; IV_xxx    8-bit       Voice queue tail
IV.FLEN:   RMB 1    ; IV_xxx    8-bit       Length of FIFO data
    ENDI
