; fujilib.inc -- 2650 client half of the FujiNet cartridge mailbox.
;
; Hand-mirrors firmware/include/fuji_mailbox.h (the single source of truth);
; every equate here is a CONSOLE address, i.e. cart image offset + 1000H for
; everything at image 1000H and up (block 2 sits at CPU $2000). Keep the two
; files in step.
;
; The cartridge connector has no write or I/O visibility, so BOTH directions
; ride reads: a "register write" is a read of FNRSEL+reg followed by a read
; of FNRDAT+value (the low address byte IS the payload), and replies are
; plain ROM bytes the cart repaints. A REGDATA read with no immediately
; preceding REGSEL read is ignored by the cart -- that pairing rule is the
; stray-read defense, so these helpers always issue the pair back to back.
;
; 2650 ground rules baked into everything here:
;   - Non-branch memory instructions reach only their own 8K page, so all
;     mailbox access from page-0 code goes through the 15-bit indirect
;     pointer words at the bottom of this file (LODA,R0 *FSELP,R1 is one
;     whole register-arm). Branch absolutes carry 15 bits and need no help.
;   - Indexed addressing puts the INDEX register in the opcode and always
;     moves data through R0. Convention: R0 data, R1/R2 arguments, R3
;     counters. Register bank 1 is never selected (RS stays 0).
;   - The return stack is the on-chip 8-entry RAS. These routines are leaves
;     or call one level down (FNSTART -> FNREGWR); tools/checkdepth.py
;     enforces the whole-client budget.
;   - Clients run with PSL = 02H (COM=1): all compares are LOGICAL. The
;     helpers rely on it. LOD instructions set CC, so a load of the 0
;     terminator is already a testable EQ.
;   - DW emits LITTLE-endian on this target but the 2650 fetches indirect
;     pointers high byte first -- every pointer word here uses DWBE. Do not
;     use DW for anything the CPU dereferences.

DWBE    MACRO   val             ; big-endian word: what indirection reads
        DB      ((val)>>8)&$FF, (val)&$FF
        ENDM

SETFSTR MACRO   adr             ; point FNTXSTR at a string (clobbers R0)
        LODI,R0 (adr)>>8
        STRA,R0 FSTRP
        LODI,R0 (adr)&$FF
        STRA,R0 FSTRP+1
        ENDM

; ---- reply bytes (repainted ROM), console addresses -------------------
FNRDATA EQU     $2B00           ; 256-byte reply slice
FNACKSQ EQU     $2C00           ; echoes SEQ when the reply is ready
FNSTATS EQU     $2C01           ; bit0 link up
FNERR   EQU     $2C02           ; fb_status_t of the last transaction
FNREPLY EQU     $2C03           ; 06H ACK / 15H NAK
FNRXLL  EQU     $2C04           ; reply length, low
FNRXLH  EQU     $2C05           ; reply length, high
FNBSTAT EQU     $2C06           ; boot state
FNBPCT  EQU     $2C07           ; boot progress 0-100
FNBERR  EQU     $2C08           ; boot error
FNMAGF  EQU     $2C09           ; 'F'
FNMAGN  EQU     $2C0A           ; 'N'
FNPVER  EQU     $2C0B           ; protocol version (1 on this platform)
FNSECHO EQU     $2C0C           ; echoes RXSLICE after each repaint

; ---- hotspot pages ----------------------------------------------------
FNRSEL  EQU     $2D00           ; read +reg: arm register
FNRDAT  EQU     $2E00           ; read +val: armed register = val
FNDATA  EQU     $2F00           ; read +val: append val to TX stream
FNSWAP  EQU     $2DFE           ; armed-only: serve the staged image

; ---- register numbers -------------------------------------------------
FRDEVIC EQU     $00
FRCMD   EQU     $01
FRNPARM EQU     $02
FRDATRS EQU     $05
FRRXSLC EQU     $06
FRSEQ   EQU     $10
FRBOOTL EQU     $11             ; BOOTLOCK; magic 0B5H arms the swap
FRBSEL1 EQU     $12             ; BOOTSEL pair: 0B5H then 4AH on FRBSEL2
FRBSEL2 EQU     $13

; ---- values -----------------------------------------------------------
FNDFUJI EQU     $70             ; FujiBus device id of the FujiNet itself
FCACFGX EQU     $C4             ; GET_ADAPTERCONFIG_EXTENDED
FCMHOST EQU     $F9             ; MOUNT_HOST
FCMIMG  EQU     $F8             ; MOUNT_IMAGE
FCSDFP  EQU     $E2             ; SET_DEVICE_FULLPATH
FCRHSL  EQU     $F4             ; READ_HOST_SLOTS
FCODIR  EQU     $F7             ; OPEN_DIRECTORY
FCRDIR  EQU     $F6             ; READ_DIR_ENTRY
FCCDIR  EQU     $F5             ; CLOSE_DIRECTORY
FCSDPS  EQU     $E4             ; SET_DIRECTORY_POSITION
FCACK   EQU     $06             ; reply command: ACK
FCNAK   EQU     $15             ; reply command: NAK
FMREAD  EQU     1               ; disk access mode: read
FEOK    EQU     0               ; FNERR values
FENOLNK EQU     1
FETMOUT EQU     2
FETIMO  EQU     $FF             ; helper-local: FNCOMMIT/FNSLICE gave up
FBIDLE  EQU     0               ; FNBSTAT values
FBXFER  EQU     1
FBREADY EQU     2
FBFAIL  EQU     $80
FNBLMAG EQU     $B5             ; BOOTLOCK magic

; ---- RAM the helpers own (zone A scratch; see the client RAM maps) ----
FSTRP   EQU     $18EB           ; FNTXSTR string pointer, big-endian word

; ---- routines ---------------------------------------------------------

; Write mailbox register: R1 = register number, R2 = value.
; Leaf. Clobbers R0.
FNREGWR:
        LODA,R0 *FSELP,R1       ; arm the register
        LODA,R0 *FDATP,R2       ; deliver the value
        RETC,UN

; Begin a transaction to the FujiNet device: rewind the TX pointer and set
; the device id. The caller then writes FRCMD and FRNPARM with its own
; FNREGWR pairs, streams params/payload, and calls FNCOMMIT.
; Calls one down. Clobbers R0,R1,R2.
FNSTART:
        LODI,R1 FRDATRS
        LODI,R2 0
        BSTA,UN FNREGWR
        LODI,R1 FRDEVIC
        LODI,R2 FNDFUJI
        BCTA,UN FNREGWR         ; tail call: FNREGWR returns to our caller

; Append a 1-byte parameter with value R2. Leaf. Clobbers R0,R1.
; (A bare TX-stream append is one instruction, no helper:
;       LODA,R0 *FTXP,Rn        with the byte in Rn.)
FNPARB:
        LODI,R1 1
        LODA,R0 *FTXP,R1        ; size byte
        LODA,R0 *FTXP,R2        ; value
        RETC,UN

; Append the 0-terminated string whose CONSOLE address is in the RAM word
; FSTRP (big-endian, caller-stored) to the TX stream. Works for strings in
; ROM, RAM, or the reply window itself (the reply-stability invariant in
; fuji_mailbox.h is what makes the last one legal). Strings cap at 255.
; Leaf. Clobbers R0,R1,R3.
FNTXSTR:
        LODI,R3 $FF
FNTXS1: LODA,R0 *FSTRP,R3,+    ; str[++R3]; a load sets CC
        BCTR,EQ FNTXS2          ; 0 terminator
        STRZ    R1
        LODA,R0 *FTXP,R1        ; append
        BCTR,UN FNTXS1
FNTXS2: RETC,UN

; Stream the 0-terminated string at (FSTRP), then NUL-pad to exactly 256
; bytes -- the fixed-buffer payload SET_DEVICE_FULLPATH / OPEN_DIRECTORY
; expect (a short read fails on the ESP32 side; the o2 client's lesson).
; Path caps at 255. Leaf. Clobbers R0,R1,R3.
FNPATH: LODI,R3 0               ; bytes sent so far (mod 256)
FNPP1:  LODA,R0 *FSTRP,R3       ; path[R3]; a load sets CC
        BCTR,EQ FNPP2           ; terminator: do not send it
        STRZ    R1
        LODA,R0 *FTXP,R1        ; append the path byte
        ADDI,R3 1
        BCTR,UN FNPP1
FNPP2:  LODI,R1 0               ; pad byte 0 (EORZ acts on R0 only, not R1)
FNPP3:  LODA,R0 *FTXP,R1        ; append 0
        ADDI,R3 1               ; until R3 wraps to 0 (256 total)
        BCFR,EQ FNPP3
        RETC,UN

; Commit the transaction and wait for the cart to acknowledge. The next
; sequence number is derived from the cart's own persisted ACKSEQ + 1
; (never a local counter: console RESET restarts this program but not the
; cart), wrapping 255 -> 1, 0 reserved.
; Returns R0 = FNERR value (0 = ok) or FETIMO on timeout (~6 s: 64K polls
; of ~90 us against the cart's own 5 s transact budget).
; Calls one down. Clobbers R1,R2,R3.
FNCOMMIT:
        LODA,R0 *FACKP
        ADDI,R0 1
        BCFR,EQ FNCM1
        LODI,R0 1               ; 255 wraps past the reserved 0
FNCM1:  STRZ    R2              ; R2 = SEQ to write = ACKSEQ to expect
        LODI,R1 FRSEQ
        BSTA,UN FNREGWR
        LODI,R3 0               ; outer x256
FNCMO:  LODI,R1 0               ; inner x256
FNCMI:  NOP                     ; pad the poll to ~90 us so the full sweep
        NOP                     ; outlasts the cart's transact timeout
        NOP
        NOP
        NOP
        NOP
        LODA,R0 *FACKP
        COMZ    R2
        BCTR,EQ FNCMOK
        BDRR,R1 FNCMI
        BDRR,R3 FNCMO
        LODI,R0 FETIMO
        RETC,UN
FNCMOK: LODA,R0 *FERRP
        RETC,UN

; Select reply slice R2 (0-3) and wait for the repaint to complete: the
; cart publishes FNSECHO last, so the slice is whole once it echoes.
; Returns R0 = 0 ok, FETIMO on timeout (~1.5 s). Preserves R2.
; Calls one down. Clobbers R0,R1,R3.
FNSLICE:
        LODI,R1 FRRXSLC
        BSTA,UN FNREGWR
        LODI,R3 64              ; outer x64
FNSLO:  LODI,R1 0               ; inner x256
FNSLI:  LODA,R0 *FSECP
        COMZ    R2
        BCTR,EQ FNSLOK
        BDRR,R1 FNSLI
        BDRR,R3 FNSLO
        LODI,R0 FETIMO
        RETC,UN
FNSLOK: EORZ    R0
        RETC,UN

; Is a FujiNet cartridge present? Returns R0 = 0 when the magic is there,
; else nonzero. Leaf. Clobbers R0.
FNCHECK:
        LODA,R0 *FMAGFP
        COMI,R0 'F'
        BCFR,EQ FNCKNO
        LODA,R0 *FMAGNP
        COMI,R0 'N'
        BCFR,EQ FNCKNO
        EORZ    R0
        RETC,UN
FNCKNO: LODI,R0 $FF
        RETC,UN

; ---- pointer words (ROM, page 0): what the indirection dereferences ----
FSELP:  DWBE    FNRSEL
FDATP:  DWBE    FNRDAT
FTXP:   DWBE    FNDATA
FRDP:   DWBE    FNRDATA
FACKP:  DWBE    FNACKSQ
FERRP:  DWBE    FNERR
FREPP:  DWBE    FNREPLY
FRXLP:  DWBE    FNRXLL
FBSTP:  DWBE    FNBSTAT
FBPCP:  DWBE    FNBPCT
FBERP:  DWBE    FNBERR
FMAGFP: DWBE    FNMAGF
FMAGNP: DWBE    FNMAGN
FSECP:  DWBE    FNSECHO
FSWAPP: DWBE    FNSWAP
