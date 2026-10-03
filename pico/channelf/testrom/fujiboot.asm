; ---------------------------------------------------------------------------
; fujiboot.asm -- M2: mount a Videocart image over the network and boot it.
;
; MOUNT_HOST -> SET_DEVICE_FULLPATH -> MOUNT_IMAGE. The last one makes the
; ESP32 side stream the image back to the cartridge on the same link, addressed
; to the DBC device, while MOUNT_IMAGE's own reply is still outstanding; the
; cart stages it and reports BOOT_STATE.
;
; The boot itself: arm the swap (BOOTLOCK), copy a 7-byte stub into the RAM
; arena and jump to it. The stub stores to FNSWAP -- the cart flips the ROM
; window to the staged image between that store and the next fetch -- then
; JMPs to $0000, where the BIOS cold-starts, re-reads the new image's own $0800
; header and runs it exactly as it would a real cartridge.
;
; The stub MUST run from the arena, because the swap replaces every byte of the
; ROM window including the code that triggered it. Every sibling port puts its
; stub in console RAM; this console has none, so the cart's own arena is the
; only memory there is -- which is why the arena survives a swap even though
; the mailbox decode does not.
;
; Target host slot and path come from bootcfg.inc, written by build.sh:
;   BOOT_HOST=0 BOOT_PATH=/hello.bin ./build.sh fujiboot
; ---------------------------------------------------------------------------
	CPU F3850

DEVSLOT	EQU 0
FMREAD	EQU 1			; disk access mode: read

; --- client RAM, in the arena at $8000 ---
VHEX	EQU 08000H		; 3 bytes: two hex digits and a NUL
VERR	EQU 08004H		; the failing code, parked across a JMP
VCLASS	EQU 08005H		; which step failed
STUB	EQU 08100H		; the swap stub, clear of the variables

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

	LI CVAL1
	LR 3,A
	LI DORGX
	LR 1,A
	LI DORGY
	LR 2,A
	DCI TSTR
	PI DSTR

	; show the target path on row 1
	LI CVAL3
	LR 3,A
	LI DORGX
	LR 1,A
	LI DORGY+DCELLH
	LR 2,A
	DCI BOOTPTH
	PI DSTR

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
	LI BOOTHST
	LR 0,A
	PI FNPB
	PI FNGO
	PI FNACK
	CI 0
	BZ MHOSTOK
	DCI VERR
	ST
	LIS 1			; class 1 = host
	DCI VCLASS
	ST
	JMP FAIL

; ---- SET_DEVICE_FULLPATH(dev, host, mode, path) ----------------------------
MHOSTOK: LI FD_FUJI
	LR 0,A
	LI FC_SDFP
	LR 1,A
	LIS 3
	LR 2,A
	PI FNBEG
	LI DEVSLOT
	LR 0,A
	PI FNPB
	LI BOOTHST
	LR 0,A
	PI FNPB
	LI FMREAD
	LR 0,A
	PI FNPB
	DCI BOOTPTH
	XDC			; FNPATH walks the path through DC1
	PI FNPATH
	PI FNGO
	PI FNACK
	CI 0
	BZ SDFPOK
	DCI VERR
	ST
	LIS 2			; class 2 = path
	DCI VCLASS
	ST
	JMP FAIL

; ---- MOUNT_IMAGE(dev, mode) ------------------------------------------------
SDFPOK:	LI CVAL2
	LR 3,A
	LI DORGX
	LR 1,A
	LI DORGY+3*DCELLH
	LR 2,A
	DCI MNTG
	PI DSTR

	LI FD_FUJI
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
	BZ MIMGOK
	DCI VERR
	ST
	LIS 3			; class 3 = mount
	DCI VCLASS
	ST
	JMP FAIL

; ---- wait for the staged image ---------------------------------------------
MIMGOK:	DCI FNBSTAT
	LM
	CI FNB_RDY
	BZ READY
	DCI FNBSTAT
	LM
	CI FNB_FAIL
	BM MIMGOK		; below FNB_FAIL: still working
	DCI FNBERR
	LM
	DCI VERR
	ST
	LIS 4			; class 4 = staging
	DCI VCLASS
	ST
	JMP FAIL

; ---- ready: arm the swap, then run the stub out of RAM ---------------------
READY:	LI CVAL3
	LR 3,A
	LI DORGX
	LR 1,A
	LI DORGY+4*DCELLH
	LR 2,A
	DCI BOOTG
	PI DSTR

	DCI FNREG+FR_BLOK
	LI FN_BLKM
	ST			; arm the swap

	DCI STUB
	XDC			; DC1 = destination
	DCI STUBSRC
	LI STUBLEN
	LR 0,A
SCPY:	LM			; A = source byte, DC0++
	XDC			; DC0 = destination, DC1 = source
	ST			; DC0(destination)++
	XDC
	DS 0
	BNZ SCPY
	JMP STUB

; The stub, assembled here but run from the arena. Seven bytes: store anything
; to FNSWAP, then cold-start the BIOS so the new image's own header runs.
STUBSRC:
	DCI FNSWAP
	ST
	JMP 00000H
STUBLEN	EQU $-STUBSRC

; ---- failure ---------------------------------------------------------------
FAIL:	LI CVAL2
	LR 3,A
	LI DORGX
	LR 1,A
	LI DORGY+6*DCELLH
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

TSTR:	DB "FUJIBOOT",0
ENOC:	DB "NO FUJINET CART",0
MNTG:	DB "MOUNTING...",0
BOOTG:	DB "BOOTING - HOLD ON",0
ESTR:	DB "ERR ",0

	INCLUDE "bootcfg.inc"
	INCLUDE "fujidisp.inc"
	INCLUDE "fujilib.inc"
	INCLUDE "font.inc"

	ORG 0800H+FN_ROM_CLAIM
	DB "FUJI"

	END
