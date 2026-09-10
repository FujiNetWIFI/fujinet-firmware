; ---------------------------------------------------------------------------
; fujicfg.asm -- M3: browse a host's directory and boot what you pick.
;
; Mount host 0, list a page of entries, move a cursor over them, descend into
; subdirectories, and boot a selection over the network.
;
; STRUCTURE. The F8 keeps exactly ONE return address (PI copies PC0 into PC1;
; POP restores it), so nesting needs PC1 saved by hand through the K register
; and the BIOS's r40-r58 stack. This program avoids the whole question: it is a
; flat state machine that JMPs between states, and every call is a single PI to
; a leaf. Nothing here is ever two deep.
;
; Two traps this port inherits from its siblings and honours:
;   - READ_DIR_ENTRY CRUNCHES the name to the maxlen you ask for, so the
;     displayed 21 characters are not a filename. Anything that has to OPEN --
;     descending or booting -- re-reads the row at full length first.
;   - NEVER read past the end of a directory. The $7F,$7F marker is the only
;     safe stop: a fujinet-pc SD host answers a position past the end with ".."
;     forever, and once that has happened every later SET_DIRECTORY_POSITION
;     NAKs for the rest of the session.
;
; Build: ../build.sh fujicfg
; ---------------------------------------------------------------------------
	CPU F3850

HOSTSL	EQU 0			; host slot to browse
DEVSLOT	EQU 0
FMREAD	EQU 1
NROWS	EQU 7			; directory rows on screen
NAMELN	EQU 21			; display width, after the cursor gutter
FULLLN	EQU 63			; re-read width for a name we must actually open

; --- client RAM, in the 30K arena. Nothing here has to be rationed: this is
; --- the one console in the family with room to spare.
VHEX	EQU 08000H		; 3 bytes
VERR	EQU 08004H
VCLASS	EQU 08005H
VCUR	EQU 08006H		; cursor row, 0..VNROW-1
VNROW	EQU 08007H		; rows actually filled on this page
VPOSL	EQU 08008H		; first directory position on this page, LE
VPOSH	EQU 08009H
VMORE	EQU 0800AH		; nonzero if the page filled completely
VISDIR	EQU 0800BH		; is the entry being opened a directory?
VROW	EQU 0800CH		; loop counter for the drawing loops
VPIXY	EQU 0800DH		; pixel row of the row being drawn
VDIR	EQU 08020H		; NROWS bytes: is that row a directory?
VPATH	EQU 08100H		; current path, NUL-terminated
VNAME	EQU 08200H		; the selected entry, at full length
VFULL	EQU 08400H		; VPATH + VNAME, the boot path
STUB	EQU 08600H		; the swap stub

	ORG 0800H
	DB 55H
	DB 00H

ENTRY:	DI

	LI CVAL0
	LR 3,A
	PI BCLRSCR
	LI PAL2A
	LR 3,A
	LI PAL2B
	LR 4,A
	PI DPAL

	; path = "/"
	DCI VPATH
	LI '/'
	ST
	CLR
	ST

	; position 0, cursor 0
	DCI VPOSL
	CLR
	ST
	CLR
	ST

	DCI VCUR
	CLR
	ST

	PI FNCHK
	BZ HAVE
	LI CVAL2
	LR 3,A
	LI DORGX
	LR 1,A
	LI DORGY+3*DCELLH
	LR 2,A
	DCI ENOC
	PI DSTR
HLTNC:	BR HLTNC

; ---- MOUNT_HOST(host) ------------------------------------------------------
HAVE:	LI FD_FUJI
	LR 0,A
	LI FC_MHOST
	LR 1,A
	LIS 1
	LR 2,A
	PI FNBEG
	LI HOSTSL
	LR 0,A
	PI FNPB
	PI FNGO
	PI FNACK
	CI 0
	BZ SDRAW
	DCI VERR
	ST
	LIS 1
	DCI VCLASS
	ST
	JMP FAIL

; ===========================================================================
; SDRAW -- repaint the whole page from the network.
;
; Stateless by construction, as on the Astrocade and INTV: every page draw is
; OPEN_DIRECTORY, then one SET_DIRECTORY_POSITION / READ_DIR_ENTRY pair per
; row, then CLOSE_DIRECTORY. Only per-row metadata survives the draw.
; ===========================================================================
SDRAW:	; header: the path we are looking at
	LI NAMELN+2
	LR 0,A
	LI DORGX
	LR 1,A
	LI DORGY
	LR 2,A
	PI DCLRR
	LI CVAL3
	LR 3,A
	LI DORGX
	LR 1,A
	LI DORGY
	LR 2,A
	DCI VPATH
	PI DSTR

	; footer
	LI NAMELN+2
	LR 0,A
	LI DORGX
	LR 1,A
	LI DORGY+8*DCELLH
	LR 2,A
	PI DCLRR
	LI CVAL1
	LR 3,A
	LI DORGX
	LR 1,A
	LI DORGY+8*DCELLH
	LR 2,A
	DCI FSTR
	PI DSTR

	; --- OPEN_DIRECTORY(host, path + NUL filter, padded to 256) ---
	LI FD_FUJI
	LR 0,A
	LI FC_ODIR
	LR 1,A
	LIS 1
	LR 2,A
	PI FNBEG
	LI HOSTSL
	LR 0,A
	PI FNPB
	DCI VPATH
	XDC
	PI FNPATH		; path, NUL, then NUL padding = an empty filter
	PI FNGO
	PI FNACK
	CI 0
	BZ SDROW
	DCI VERR
	ST
	LIS 2
	DCI VCLASS
	ST
	JMP FAIL

	; --- one row at a time ---
SDROW:	DCI VNROW
	CLR
	ST
	DCI VMORE
	CLR
	ST

	; Seek ONCE per page, and only when the page does not start at the top:
	; a freshly opened directory is already at position 0, and dir_seek(0)
	; fails outright on an empty directory (fnFsSD.cpp requires
	; pos < _dir_entries.size()), which would read as an error rather than as
	; the empty listing it is. The reads that follow are sequential, so this
	; also costs one round trip per page instead of one per row.
	DCI VPOSL
	LM
	LR 4,A
	LM
	LR 5,A
	LR A,4
	OI 0
	BNZ SDSEEK
	LR A,5
	OI 0
	BZ SDLOOP

SDSEEK:	LI FD_FUJI
	LR 0,A
	LI FC_SDPOS
	LR 1,A
	LIS 1
	LR 2,A
	PI FNBEG
	LR A,4
	LR 0,A
	LR A,5
	LR 1,A
	PI FNPW
	PI FNGO
	PI FNACK
	CI 0
	BNZ SDEND		; past the end: show an empty page rather than fail

SDLOOP:	; blank the row first: DSTR only sets pixels, so stale text would show
	; through a shorter name
	LI NAMELN+2
	LR 0,A
	LI DORGX
	LR 1,A
	DCI VNROW
	LM
	INC			; screen row = list row + 1
	LR 2,A
	LI DCELLH
	LR 5,A
	CLR
	LR 6,A
SDMUL:	LR A,6			; r6 = row * DCELLH
	AS 5
	LR 6,A
	DS 2
	BNZ SDMUL
	LR A,6
	AI DORGY
	LR 2,A
	PI DCLRR

	; READ_DIR_ENTRY(NAMELN, 0)
	LI FD_FUJI
	LR 0,A
	LI FC_RDIR
	LR 1,A
	LIS 2
	LR 2,A
	PI FNBEG
	LI NAMELN
	LR 0,A
	PI FNPB
	CLR
	LR 0,A
	PI FNPB
	PI FNGO
	PI FNACK
	CI 0
	BNZ SDEND
	PI FNEOF
	BZ SDEND		; the $7F,$7F marker: stop, and do not read on

	; draw it, one cell in from the cursor gutter
	LI CVAL1
	LR 3,A
	LI DORGX+DCELLW
	LR 1,A
	PI SDROWY		; r2 = pixel row for this list row
	DCI FNREPLY
	PI DSTR

	; a directory arrives with a trailing '/' from the firmware; there is no
	; separate flag
	PI SDISDIR		; r0 = 1 if the reply name ends in '/'
	DCI VNROW
	LM
	LR 1,A
	DCI VDIR
	LR A,1
	ADC			; DC0 = VDIR + row
	LR A,0
	ST

	DCI VNROW
	LM
	INC
	LR 0,A
	DCI VNROW
	ST
	CI NROWS
	BZ SDFULL		; BNZ cannot reach SDLOOP: invert and JMP
	JMP SDLOOP
SDFULL:	DCI VMORE
	LIS 1
	ST			; the page filled: there may be more

SDEND:	; CLOSE_DIRECTORY
	LI FD_FUJI
	LR 0,A
	LI FC_CDIR
	LR 1,A
	CLR
	LR 2,A
	PI FNBEG
	PI FNGO

	; clear any rows this page did not fill. The counter lives in RAM, not a
	; register: DCLRR clobbers r0 and r4-r8, which is most of the directly
	; addressable file, and an earlier version of this loop kept its counter
	; in r7 and never terminated.
	DCI VNROW
	LM
	DCI VROW
	ST
SDBLNK:	DCI VROW
	LM
	CI NROWS
	BZ SDCUR
	DCI VROW
	LM
	INC
	LR 6,A
	LI DCELLH
	LR 5,A
	CLR
	LR 2,A
SDBM:	LR A,2
	AS 5
	LR 2,A
	DS 6
	BNZ SDBM
	LR A,2
	AI DORGY
	LR 2,A
	LI NAMELN+2
	LR 0,A
	LI DORGX
	LR 1,A
	PI DCLRR
	DCI VROW
	LM
	INC
	DCI VROW
	ST
	BR SDBLNK

	; clamp the cursor into the page
SDCUR:	DCI VNROW
	LM
	LR 0,A
	CI 0
	BZ SDCUR0
	DCI VCUR
	LM
	LR 1,A
	LR A,0
	CI 0			; compare cursor against row count
	LR A,1
	LR 2,A
	LR A,0
	LR 3,A
	LR A,2
	LR 4,A
	; if cursor >= nrow, put it on the last row
	LR A,3
	COM
	INC
	AS 4			; A = cursor - nrow
	BM SDCURK		; negative: cursor is inside the page
	LR A,3
	AI 0FFH
	DCI VCUR
	ST
	BR SDCURK
SDCUR0:	DCI VCUR
	CLR
	ST
SDCURK:	JMP SCUR

; ---- helpers used by SDRAW (leaves) ---------------------------------------

; SDROWY -- r2 = the pixel row for list row VNROW.
;   Clobbers A, r2, r5, r6.
SDROWY:	DCI VNROW
	LM
	INC
	LR 6,A
	LI DCELLH
	LR 5,A
	CLR
	LR 2,A
SDRY1:	LR A,2
	AS 5
	LR 2,A
	DS 6
	BNZ SDRY1
	LR A,2
	AI DORGY
	LR 2,A
	POP

; SDISDIR -- r0 = 1 if the name in the reply window ends with '/', else 0.
;   Clobbers A, r0, r1, DC0.
SDISDIR: DCI FNREPLY
	CLR
	LR 1,A			; last non-NUL byte seen
SDID1:	LM
	CI 0
	BZ SDID2
	LR 1,A
	BR SDID1
SDID2:	LR A,1
	CI '/'
	BZ SDIDY
	CLR
	LR 0,A
	POP
SDIDY:	LIS 1
	LR 0,A
	POP

; ===========================================================================
; SCUR -- draw the cursor gutter: a chevron on the selected row, blank on the
; others. Redrawing only the gutter is what makes moving the cursor free -- the
; row text never has to be fetched again, so a keypress costs no network round
; trip. (The ColecoVision inverts the row instead, which needs VRAM readback;
; this console has none.)
; ===========================================================================
SCUR:	DCI VROW
	CLR
	ST
SCUR1:	DCI VROW
	LM
	CI NROWS
	BZ SCURX

	; pixel row = DORGY + (list row + 1) * DCELLH, parked in RAM because
	; DCLRR is about to clobber every register that could hold it
	DCI VROW
	LM
	INC
	LR 6,A
	LI DCELLH
	LR 5,A
	CLR
	LR 2,A
SCURM:	LR A,2
	AS 5
	LR 2,A
	DS 6
	BNZ SCURM
	LR A,2
	AI DORGY
	DCI VPIXY
	ST

	LIS 1
	LR 0,A
	LI DORGX
	LR 1,A
	DCI VPIXY
	LM
	LR 2,A
	PI DCLRR

	DCI VCUR
	LM
	LR 0,A
	DCI VROW
	LM
	XS 0
	BNZ SCURN		; not the selected row: leave the gutter blank
	LI CVAL2
	LR 3,A
	LI DORGX
	LR 1,A
	DCI VPIXY
	LM
	LR 2,A
	DCI CURSTR
	PI DSTR
SCURN:	DCI VROW
	LM
	INC
	DCI VROW
	ST
	BR SCUR1
SCURX:	JMP SINPUT

; ===========================================================================
; SINPUT -- the idle loop.
; ===========================================================================
; Every arm is inverted-and-JMP rather than a direct BZ: the state bodies are
; far bigger than a relative branch's +/-127 reach, and inverting keeps that
; true no matter how they grow. CI leaves A alone, so the chain can keep
; testing the event; JMP clobbers A, which is fine once we are leaving.
SINPUT:	PI INSCAN
	LR A,0
	CI EV_NONE
	BZ SINPUT
	CI EV_UP
	BNZ SID1
	JMP SIUP
SID1:	CI EV_DOWN
	BNZ SID2
	JMP SIDOWN
SID2:	CI EV_SEL
	BNZ SID3
	JMP SISEL
SID3:	CI EV_PGDN
	BNZ SID4
	JMP SIPGDN
SID4:	CI EV_PGUP
	BNZ SID5
	JMP SIPGUP
SID5:	CI EV_BACK
	BNZ SINPUT
	JMP SIBACK

SIUP:	DCI VCUR
	LM
	CI 0
	BZ SINPUT		; already at the top; paging is explicit
	AI 0FFH
	DCI VCUR
	ST
	JMP SCUR

SIDOWN:	DCI VCUR
	LM
	LR 0,A
	INC
	LR 1,A
	DCI VNROW
	LM
	LR 2,A
	LR A,1
	COM
	INC
	AS 2			; nrow - (cur+1)
	BM SINPUT		; would run past the last filled row
	BZ SINPUT
	LR A,1
	DCI VCUR
	ST
	JMP SCUR

SIPGDN:	DCI VMORE
	LM
	CI 0
	BZ SINPUT		; the page did not fill: there is no next page
	DCI VPOSL
	LM
	AI NROWS
	LR 0,A
	LM
	LR 1,A
	LR A,0
	BNZ SIPD1
	LR A,1
	INC
	LR 1,A
SIPD1:	DCI VPOSL
	LR A,0
	ST
	LR A,1
	ST
	DCI VCUR
	CLR
	ST
	JMP SDRAW

SIPGUP:	DCI VPOSL
	LM
	LR 0,A
	LM
	LR 1,A
	LR A,1
	CI 0
	BNZ SIPU1		; high byte set: definitely past the first page
	LR A,0
	CI NROWS
	BM SIPUZ		; below one page: go to the start
SIPU1:	LR A,0
	AI -NROWS
	LR 0,A
	BNZ SIPU2
SIPU2:	LR A,0
	CI 0FFH-NROWS+1
	BM SIPU3
	LR A,1
	CI 0
	BZ SIPU3
	LR A,1
	AI 0FFH
	LR 1,A
SIPU3:	DCI VPOSL
	LR A,0
	ST
	LR A,1
	ST
	BR SIPUD
SIPUZ:	DCI VPOSL
	CLR
	ST
	CLR
	ST
SIPUD:	DCI VCUR
	CLR
	ST
	JMP SDRAW

SIBACK:	PI PATHUP
	DCI VPOSL
	CLR
	ST
	CLR
	ST
	DCI VCUR
	CLR
	ST
	JMP SDRAW

; ===========================================================================
; SISEL -- re-read the highlighted entry at full length, then descend or boot.
; ===========================================================================
SISEL:	DCI VNROW
	LM
	CI 0
	BNZ SISHAVE		; BZ cannot reach SINPUT: invert and JMP
	JMP SINPUT		; nothing on this page to pick
SISHAVE:

	; is the highlighted row a directory?
	DCI VCUR
	LM
	LR 1,A
	DCI VDIR
	LR A,1
	ADC
	LM
	DCI VISDIR
	ST			; RAM, not r9: FNGO stores the sequence in r9 on
				; every commit, and this has to survive several

	; re-open the directory and re-read this row at FULLLN: the drawn name
	; was crunched to NAMELN and is not a filename
	LI FD_FUJI
	LR 0,A
	LI FC_ODIR
	LR 1,A
	LIS 1
	LR 2,A
	PI FNBEG
	LI HOSTSL
	LR 0,A
	PI FNPB
	DCI VPATH
	XDC
	PI FNPATH
	PI FNGO

	LI FD_FUJI
	LR 0,A
	LI FC_SDPOS
	LR 1,A
	LIS 1
	LR 2,A
	PI FNBEG
	DCI VPOSL
	LM
	LR 4,A
	LM
	LR 5,A
	DCI VCUR
	LM
	AS 4
	LR 0,A
	LR A,5
	LR 1,A
	LR A,0
	BNZ SISNOC
	LR A,1
	INC
	LR 1,A
SISNOC:	PI FNPW
	PI FNGO

	LI FD_FUJI
	LR 0,A
	LI FC_RDIR
	LR 1,A
	LIS 2
	LR 2,A
	PI FNBEG
	LI FULLLN
	LR 0,A
	PI FNPB
	CLR
	LR 0,A
	PI FNPB
	PI FNGO
	PI FNACK
	CI 0
	BNZ SISBAD

	; copy the full name out of the reply window before CLOSE_DIRECTORY --
	; a CLOSE repaints the window and would wipe it
	DCI VNAME
	XDC
	DCI FNREPLY
SISCP:	LM
	XDC
	ST
	XDC
	CI 0
	BNZ SISCP
	XDC

	LI FD_FUJI
	LR 0,A
	LI FC_CDIR
	LR 1,A
	CLR
	LR 2,A
	PI FNBEG
	PI FNGO

	DCI VISDIR
	LM
	CI 0
	BZ SBOOT

	; a directory: append it to the path and redraw from the top
	PI PATHAPP
	DCI VPOSL
	CLR
	ST
	CLR
	ST
	DCI VCUR
	CLR
	ST
	JMP SDRAW

SISBAD:	DCI VERR
	ST
	LIS 5
	DCI VCLASS
	ST
	JMP FAIL

; ===========================================================================
; SBOOT -- mount the selection and swap it in.
; ===========================================================================
SBOOT:	LI NAMELN+2
	LR 0,A
	LI DORGX
	LR 1,A
	LI DORGY+8*DCELLH
	LR 2,A
	PI DCLRR
	LI CVAL2
	LR 3,A
	LI DORGX
	LR 1,A
	LI DORGY+8*DCELLH
	LR 2,A
	DCI MNTG
	PI DSTR

	PI PATHFUL		; VFULL = VPATH + VNAME

	; SET_DEVICE_FULLPATH(dev, host, mode, path)
	LI FD_FUJI
	LR 0,A
	LI FC_SDFP
	LR 1,A
	LIS 3
	LR 2,A
	PI FNBEG
	LI DEVSLOT
	LR 0,A
	PI FNPB
	LI HOSTSL
	LR 0,A
	PI FNPB
	LI FMREAD
	LR 0,A
	PI FNPB
	DCI VFULL
	XDC
	PI FNPATH
	PI FNGO
	PI FNACK
	CI 0
	BZ SBMNT
	DCI VERR
	ST
	LIS 6
	DCI VCLASS
	ST
	JMP FAIL

SBMNT:	LI FD_FUJI
	LR 0,A
	LI FC_MIMG
	LR 1,A
	LIS 2
	LR 2,A
	PI FNBEG
	LI DEVSLOT
	LR 0,A
	PI FNPB
	LI FMREAD
	LR 0,A
	PI FNPB
	PI FNGO
	PI FNACK
	CI 0
	BZ SBWAIT
	DCI VERR
	ST
	LIS 7
	DCI VCLASS
	ST
	JMP FAIL

SBWAIT:	DCI FNBSTAT
	LM
	CI FNB_RDY
	BZ SBGO
	DCI FNBSTAT
	LM
	CI FNB_FAIL
	BM SBWAIT
	DCI FNBERR
	LM
	DCI VERR
	ST
	LIS 8
	DCI VCLASS
	ST
	JMP FAIL

SBGO:	DCI FNREG+FR_BLOK
	LI FN_BLKM
	ST			; arm the swap

	DCI STUB
	XDC
	DCI STUBSRC
	LI STUBLEN
	LR 0,A
SBCPY:	LM
	XDC
	ST
	XDC
	DS 0
	BNZ SBCPY
	JMP STUB

; The stub, assembled here but run from the arena: the swap replaces every byte
; of the ROM window, including whatever code triggered it.
STUBSRC:
	DCI FNSWAP
	ST
	JMP 00000H
STUBLEN	EQU $-STUBSRC

; ---- path helpers (leaves) -------------------------------------------------

; PATHAPP -- append the NUL-terminated VNAME to VPATH.
;   Clobbers A, DC0, DC1.
PATHAPP: DCI VPATH
PAPP1:	LM
	CI 0
	BNZ PAPP1
	LI 0FFH
	ADC			; LM left DC0 one past the NUL; step back onto it,
				; because that is where the name begins
	XDC			; DC1 = write cursor
	DCI VNAME
PAPP2:	LM			; A = name byte, DC0 walks VNAME
	XDC			; DC0 = write cursor, DC1 = name cursor
	ST
	XDC
	CI 0
	BNZ PAPP2		; the terminator is copied too
	POP

; PATHUP -- drop the last component of VPATH, leaving the trailing '/'.
;   "/a/b/" becomes "/a/", and "/" stays "/".
;   Clobbers A, r0, r1, DC0.
PATHUP:	DCI VPATH
	CLR
	LR 0,A			; index
	LR 1,A			; index of the '/' before the last one
PUP1:	LM
	CI 0
	BZ PUP2
	CI '/'
	BNZ PUPN
	LR A,0
	LR 1,A			; remember where this '/' was
PUPN:	LR A,0
	INC
	LR 0,A
	BR PUP1
PUP2:	LR A,1
	CI 0
	BZ PUPX			; only the root '/': nothing to drop
	; VPATH[r1] is the trailing '/'; find the one before it
	DCI VPATH
	CLR
	LR 0,A
	CLR
	LR 2,A
PUP3:	LR A,0
	XS 1
	BZ PUP4			; reached the trailing '/'
	LM
	CI '/'
	BNZ PUPN2
	LR A,0
	LR 2,A
	BR PUPN2
PUPN2:	LR A,0
	INC
	LR 0,A
	BR PUP3
PUP4:	DCI VPATH
	LR A,2
	INC
	ADC			; DC0 = VPATH + previous '/' + 1
	CLR
	ST			; terminate there
PUPX:	POP

; PATHFUL -- VFULL = VPATH followed by VNAME.
;   Clobbers A, DC0, DC1.
PATHFUL: DCI VFULL
	XDC
	DCI VPATH
PF1:	LM
	CI 0
	BZ PF2
	XDC
	ST
	XDC
	BR PF1
PF2:	DCI VNAME
PF3:	LM
	XDC
	ST
	XDC
	CI 0
	BNZ PF3
	POP

; ---- failure ---------------------------------------------------------------
FAIL:	LI NAMELN+2
	LR 0,A
	LI DORGX
	LR 1,A
	LI DORGY+8*DCELLH
	LR 2,A
	PI DCLRR
	LI CVAL2
	LR 3,A
	LI DORGX
	LR 1,A
	LI DORGY+8*DCELLH
	LR 2,A
	DCI ESTR
	PI DSTR
	DCI VCLASS
	LM
	LR 0,A
	DCI VHEX
	PI DHEXS
	DCI VHEX
	PI DSTR
	DCI VERR
	LM
	LR 0,A
	DCI VHEX
	PI DHEXS
	DCI VHEX
	PI DSTR
HLTF:	BR HLTF

ENOC:	DB "NO FUJINET CART",0
FSTR:	DB "FIRE:PICK MODE:UP",0
MNTG:	DB "MOUNTING...",0
ESTR:	DB "ERR ",0
CURSTR:	DB ">",0

	INCLUDE "fujidisp.inc"
	INCLUDE "fujilib.inc"
	INCLUDE "input.inc"
	INCLUDE "font.inc"

	ORG 0800H+FN_ROM_CLAIM
	DB "FUJI"

	END
