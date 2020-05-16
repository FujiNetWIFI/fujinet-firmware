	
;	Render Glyph
;	Args:
;	cx, cy (coordinates)
;	CharCode (character)
;	GlyphData (address of glyph data)
;	Flags: bit 7: 1 = reverse video, 0 = normal
;	bit 6: 1 = double size, 0 = normal
;	bit 5: 1 = transparent background mode, 0 = normal
;	bit 4: 1 = render in background colour with transparent background
;
;	320 x 192 screen size is assumed
;	Note: no checks are made for x >= 320 or y >= 192
;	Please add these if necessary

.include "zeropage.inc"
.include "atari.inc"
.import _cx, _cy, _CharCode, _GlyphData, _Flags
.export _RenderGlyph

.define PixelOffset tmp1
.define LineCount tmp2
.define ByteExtent tmp3
.define LeftMask tmp4

.segment	"EXTZP":zeropage
RightMask:		.res 1  ; right hand mask
MiddleMask:		.res 1	; middle mask
ByteOffset:		.res 1  ; horizontal byte offset on screen
EORMask:		.res 1  ; EOR mask (for reverse video rendering)
BitmapBuffer:		.res 3  ; character data buffer (in shifted position)
DoubleFlag:		.res 1	; repeat line buffer flag
LeftBit:		.res 1 	; Left bit to shift in for AND mode
	
.code
	
.proc _RenderGlyph
	lda _Flags 	; check for background colour render
	asl
	asl
	asl
	and #$80
	sta LeftBit	; if we're rendering black-on-white, bitmask needs to have 1 shifted into top bit
	
	ldx #0
	stx DoubleFlag	; repeated buffer flag for double-height glyphs
	stx MiddleMask
	
	lda _Flags
	and #$90
	beq :+
	ldx #%11111000
	and #$10
	beq :+
	ldx #%11111111
:
	stx EORMask
	
	jsr GetScreenAddress	; load up ScreenAddress and get pixel offset
	jsr GetGlyphAddress		; figure out address of glyph data

	ldx #6	; glyph height
	lda #5	; pixel width
	bit _Flags
	bvc :+
	asl	; double pixel width if we're in double size mode
	ldx #12
:
	stx LineCount
	clc
	adc PixelOffset	; calculate total pixel extent including x offset
	pha
	and #$07
	tax
	lda RightBGMaskTable,x
	sta RightMask
	pla
	lsr
	lsr
	lsr
	sta ByteExtent
	
	ldx PixelOffset	; set up left and right background masks
	lda LeftBGMaskTable,x
	sta LeftMask
	
	ldx ByteExtent
	bne :+
	
	ora RightMask	; if character only occupies one byte, OR in the right background mask
	sta LeftMask
	
:
 	lda #$20	; check for transparent background mode
 	and _Flags
 	beq NotTransparent
 	
 	lda #$FF	; for transparent mode, we simply keep all the background bits intact
 	sta LeftMask
 	sta RightMask
 	sta MiddleMask
 	
NotTransparent:	
 	lda #192	; trim off bottom of character if it extends off the foot of the screen
	sec
	sbc _cy
	cmp LineCount	; we now account for double-height glyphs
	bcs Loop1
	sta LineCount

Loop1:	
	ldx #0
	lda #$10
	bit _Flags
	beq :+
	dex
:
	stx BitmapBuffer
	stx BitmapBuffer+1
	stx BitmapBuffer+2

	bit _Flags
	bvc NormalSize
	lda #$80
	sta DoubleFlag
	jsr CreateDoubledBitmap
	jmp DoShift
	
NormalSize:	
	ldy #0
	lda (ptr2),y
	eor EORMask

DoShift:	
	ldx PixelOffset
	beq NoShift
:
	lsr
	ora LeftBit
	ror BitmapBuffer+1
	ror BitmapBuffer+2
	dex
	bne :-
NoShift:	
	sta BitmapBuffer	; bitmap is now in desired x position

DoRender:	
	lda ByteExtent
	sta ptr3

	ldy ByteOffset
	ldx #1

	lda #$10
	bit _Flags 	; check for background colour render
	beq NormalColour
	
	lda BitmapBuffer	; bits are already reversed
	and (ptr1),y
	sta (ptr1),y
	iny
	cpy #40
	bcs Done
	
	dec ptr3
	bmi Done
	beq LastByte2
	
	lda BitmapBuffer+1
	and (ptr1),y
	sta (ptr1),y
	iny
	cpy #40
	bcs Done
	inx
	
LastByte2:	
	lda BitmapBuffer,x
	and (ptr1),y
	sta (ptr1),y
	jmp Done
	
NormalColour:	
	lda (ptr1),y
	and LeftMask
	ora BitmapBuffer
	sta (ptr1),y
	iny
	cpy #40	; omit this check if you don't intend to let text run off the screen
	bcs Done
	
	dec ptr3
	bmi Done
	beq LastByte
	
	lda (ptr1),y
	and MiddleMask
	ora BitmapBuffer+1
	sta (ptr1),y
	iny
	cpy #40	; omit this check if you don't intend to let text run off the screen
	bcs Done
	inx

LastByte:	
	lda (ptr1),y
	and RightMask
	ora BitmapBuffer,x
	sta (ptr1),y
Done:	

	lsr DoubleFlag	; shift bit 7 into bit 6 (flag will clear on second iteration)

	dec LineCount
	beq Finished
	
	lda ptr1
	clc
	adc #40
	sta ptr1
	bcc :+
	inc ptr1+1
:	

	bit DoubleFlag	; if bit 6 set, we just repeat the previous line using existing buffer content
	bvc :+
	jmp DoRender
:

	inc ptr2
	bne :+
	inc ptr2+1
:
	jmp Loop1
	
Finished:	
	rts
.endproc




;	Work out the address of the target line
;	Note: would be much faster using a look-up table, but that would cost ~400 bytes

.proc GetScreenAddress
	lda #0
	sta ptr1+1
	lda _cy

	asl	; work out y * 8
	rol ptr1+1
	asl
	rol ptr1+1
	asl
	rol ptr1+1
	
	sta ptr3
	ldy ptr1+1
	sty ptr3+1
	
	asl	; work out y * 32
	rol ptr1+1
	asl
	rol ptr1+1
	
	clc
	adc ptr3	; add y * 8 to get y * 40
	sta ptr1
	tya
	adc ptr1+1
	sta ptr1+1
	
	lda _cx+1	; work out x / 8 to get horizontal byte offset
	sta ptr3+1
	lda _cx
	tay
	and #$07	; get pixel offset
	sta PixelOffset
	tya
	
	lsr ptr3+1
	ror
	lsr ptr3+1
	ror
	lsr ptr3+1
	ror
	sta ByteOffset

	lda ptr1	; finally, add screen base address
	clc
	adc SAVMSC
	sta ptr1
	lda ptr1+1
	adc SAVMSC+1
	sta ptr1+1
	rts
.endproc





;	Work out offset into glyph data

.proc GetGlyphAddress
	lda #0
	sta ptr2+1
	lda _CharCode
	
	asl	; work out char * 6
	rol ptr2+1
	sta ptr3
	ldy ptr2+1
	sty ptr3+1
	
	asl
	rol ptr2+1
	
	clc
	adc ptr3	; add offset * 2 to offset * 4
	sta ptr2
	
	lda ptr2+1
	adc ptr3+1
	sta ptr2+1
	
	lda ptr2	; add in base address of font
	clc
	adc _GlyphData
	sta ptr2
	lda ptr2+1
	adc _GlyphData+1
	sta ptr2+1
	rts
.endproc




;	Create double width bitmap

.proc CreateDoubledBitmap
	ldy #0
	lda (ptr2),y
	eor EORMask
	lsr
	lsr
	lsr
	ldx #4	; loop for each pixel
:
	lsr	; shift rightmost bit into c
	php	; save state of carry
	ror BitmapBuffer
	ror BitmapBuffer+1	; this leaves c = 0
	plp	; retrieve carry
	ror BitmapBuffer
	ror BitmapBuffer+1
	dex
	bpl :-
	lda BitmapBuffer
	rts
.endproc

.rodata
	
LeftBGMaskTable:	; 	LUT for left hand mask
	.byte %00000111
	.byte %10000011
	.byte %11000001
	.byte %11100000
	.byte %11110000
	.byte %11111000
	.byte %11111100
	.byte %11111110
	
	
RightBGMaskTable:	; LUT for right hand mask
	.byte %11111111
	.byte %01111111
	.byte %00111111
	.byte %00011111
	.byte %00001111
	.byte %00000111
	.byte %00000011
	.byte %00000001
	

;; ;	you can probably move some of the most used absolute address variables into ptr4 and tmp1-tmp4

;; PixelOffset		.ds 1	; pixel offset into leftmost byte
;; cy				.ds 1	; y coord
;; cx 				.ds 2	; x coord
;; LineCount		.ds 1	; line counter
;; CharCode		.ds 1	; character to display
;; GlyphData		.ds 2	; base address of character data
;; Flags			.ds 1	; bit 7: 1 = reverse video, 0 = normal; bit 6: 1 = double size, 0 = normal
;; ByteExtent		.ds 1	; number of bytes fully or partially occupied by glyph on screen
;; LeftMask		.ds 1	; left hand mask
;; RightMask		.ds 1	; right hand mask
;; MiddleMask		.ds 1	; middle mask
;; ByteOffset		.ds 1	; horizontal byte offset on screen
;; EORMask			.ds 1	; EOR mask (for reverse video rendering)
;; BitmapBuffer	.ds 3	; character data buffer (in shifted position)
;; DoubleFlag		.ds 1	; repeat line buffer flag
;; LeftBit			.ds 1	; bit to shift into rotated bitmask
	
;; 	run Start
