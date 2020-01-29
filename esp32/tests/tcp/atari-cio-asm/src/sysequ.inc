;
;                    ATARI 800 EQUATE LISTING
;
; (version 20070530_bkw)
;
; This is a heavily modified copy of Appendix A of the Atari System
; Reference Manual (with much info added from Appendix B, and from
; Mapping the Atari and other sources)
;
;
;This listing is based on the original release of Operating System,
;version A.  The vectors shown here were not changed in version B.
;New equates for XL and XE models are included and noted.  Changes
;from version B to XL/XE are also noted.
;
;Most of the equate names given below are the official Atari
;names.  They are in common use but are not mandatory.

; This file can be included in your assembly source, but it's also
; got a lot of useful human-readable comments. It's meant to serve as
; a "quick reference" to Atari programmers, particularly ones who use
; a cross-assembler on a UNIX-ish platform and a text editor that can
; use "ctags":

; $ ctags equates.inc
; $ vim mystuff.dasm

; While in vim, press ^] (control-right-bracket) while sitting on a label,
; to jump to that label's definition in this file. (Also, you can type
; :tag labelname). You can also do completion on the labels in vim by
; typing part of a label and pressing ^N (or Tab, if you use the
; CleverTab script from vimhelp.org)

; GNU Emacs and XEmacs also support ctags, but I've never used them, so
; I dunno how to do it. I do know that you need to use "Exuberant" ctags,
; not the ctags that comes with emacs (which doesn't grok 6502 asm).

; If you're using an Atari assembler instead of a cross-assembler, you
; don't want to use this as-is: all the extra comments make it huge, and
; it'll be either too large for the Atari's memory, or at least will take
; a long time to assemble. You can make a comment-less, Atari-compatible
; version like so:

; perl -lne 's/;.*//; s/\s*$//; print if $_' < equates.inc > small.inc

; ...then use a8eol to convert it to ATASCII format.

; 20061028 bkw: Originally downloaded from:

; http://atrey.karlin.mff.cuni.cz/~pavel/atari/atrb.html

; ...and converted to DASM/ATASM/CA65 format. dasm, atasm, and ca65 use
; similar enough syntax that this file can be used as-is with any of
; them. Unfortunately, this means I can't do conditional assembly in this
; file, since the two assemblers use different semantics... macros are
; even less compatible :(

; If you use ca65, you need this line in your source:

;; .FEATURE labels_without_colons

; before including this file, or else run ca65 with
; "--feature labels_without_colons").

; 20070529 bkw: updated, added missing GTIA/POKEY/ANTIC equates,
; documented where the shadows are for those GTIA/POKEY/ANTIC/PIA
; registers that have them. Also added a list of error messages and
; explanation of the cassette buffer layout, and organized the C_*
; CIO constants. Made minor modifications to get this file to assemble
; with ATasm as well as DASM.

; I have added a few missing equates: this file only
; contained OS ROM locations when I got it.
; I added a few from FMS/DOS as well (e.g. RUNAD and INITAD).

; XL-specific locations in the original file were duplicate labels
; (e.g. PTIMOT was defined as both $1C and $314), which keeps DASM
; from being able to assemble the file. I prefixed the XL/XL versions
; with "XL_"

; Also, I've prefixed the CIO command and AUX1 constants with C_, since some
; of them conflicted with other labels in the original version.

; Areas listed as "unmapped" are literally not connected to anything.
; Trying to read from unmapped address space results in reading whatever
; garbage was on the data bus when the read happened. On my 1200XL, this
; generally results in all 1's ($FF or 255).
; In a 400, 600XL or other Atari with less than 48K of RAM, the missing
; RAM address space is also unmapped.

; Still TODO:
; - Rest of the DOS/FMS equates
; - Mark 800-ony locations with OSB_
; - ifdef code, so the user can set the machine type (OSB or XL),
;   then refer to e.g. PTIMOT and get either OSB_PTIMOT or XL_PTIMOT
; - Split into separate files? I'd rather not (it's only about 1000 lines)

; This file's mostly intended for new development. It could also be
; useful for porting old ASM/ED or Mac65 code to DASM, but such code
; may need work... you can always assemble it with ATasm, in that case,
; since it's 99.999% source code compatible with Mac65.

; References to "APPENDIX C" and such are referring to the Atari System
; Reference Manual, a version of which can be found at:

; http://atrey.karlin.mff.cuni.cz/~pavel/atari/atrtblc.html

; References to "Mapping" refer to "Mapping the Atari, Revised Edition",
; which can be found at:

; http://www.atariarchives.org/mapping/index.php

; I've pasted a few quotes from Mapping into this file; I consider them
; small enough to be covered under the "fair use" provisions of copyright law
; (I am not a lawyer, though).

;
;
;       DEVICE NAMES
;
;
;SCREDT = "E"   SCREEN EDITOR
;KBD    = "K"   KEYBOARD
;DISPLY = "S"   DISPLAY
;PRINTR = "P"   PRINTER
;CASSET = "C"   CASSETTE
;DISK   = "D"   DISK DRIVE
;
;
;
;       STATUS  CODES
;
;

; 20070529 bkw: These are returned as error codes, though various DOSes
; also define their own codes (usually in the range 160-255).
; Errors 2-21 are defined by BASIC.
; Errors 150-154 are defined by the R: (850 or compatible, RS-232) handler.
SUCCES = $01    ;    1
BRKABT = $80    ;  128 BREAK KEY ABORT
PRVOPN = $82    ;  130 IOCB ALREADY OPEN
NONDEV = $82    ;  130 NONEXISTANT DEVICE
WRONLY = $83    ;  131 OPENED FOR WRITE ONLY
NVALID = $84    ;  132 INVALID COMMAND
NOTOPN = $85    ;  133 DEVICE OR FILE NOT OPEN
BADIOC = $86    ;  134 INVALID IOCB NUMBER
RDONLY = $87    ;  135 OPENED FOR READ ONLY
EOFERR = $88    ;  136 END OF FILE
TRNRCD = $89    ;  137 TRUNCATED RECORD
TIMOUT = $8A    ;  138 PERIPHERAL TIME OUT
DNACK  = $8B    ;  139 DEVICE DOES NOT ACKNOWLEDGE
FRMERR = $8C    ;  140 SERIAL BUS FRAMING ERROR
CRSROR = $8D    ;  141 CURSOR OUT OF RANGE
OVRRUN = $8E    ;  142 SERIAL BUS DATA OVERRUN
CHKERR = $8F    ;  143 SERIAL BUS CHECKSUM ERROR
DERROR = $90    ;  144 PERIPHERAL DEVICE ERROR
BADMOD = $91    ;  145 NON EXISTANT SCREEN MODE
FNCNOT = $92    ;  146 FUNCTION NOT IMPLEMENTED
SCRMEM = $93    ;  147 NOT ENOUGH MEMORY FOR SCREEN MODE

; BASIC error codes (also used by e.g. Basic XL/XE and Turbo BASIC):
;; 2: Insufficient Memory
;; 3: Value Error
;; 4: Too Many Variables
;; 5: String Length Error
;; 6: Out of Data Error
;; 7: Number Greater than 32767
;; 8: Input Statement Error
;; 9: Array or String DIM Error
;; 10: Argument Stack Overflow
;; 11: Floating Point Overflow or Underflow Error
;; 12: Line Not Found
;; 13: No Matching FOR Statement
;; 14: Line Too Long
;; 15: GOSUB or FOR Line Deleted
;; 16: RETURN Error
;; 17: Garbage Error
;; 18: Invalid String Character
;; 19: LOAD Program Too Long
;; 20: Bad Channel Number
;; 21: LOAD File Error

; 850/R: error codes:
;; 150: Serial Port Already Open
;; 151: Concurrent Mode Not Enabled
;; 152: Illegal User-Supplied Buffer
;; 153: Active Concurrent Mode Error
;; 154: Concurrent Mode Not Active

; DOS error codes (DOS 2.0S only; other DOSes may define other errors)
;; 160: Device Number Error
;; 161: Too Many OPEN Files
;; 162: Disk Full
;; 163: Fatal System Error
;; 164: File Number Mismatch
;; 165: Bad File Name
;; 166: POINT Data Length Error
;; 167: File Locked
;; 168: Invalid XIO Command
;; 169: Directory Full
;; 170: File Not Found
;; 171: POINT Invalid
;; 172: DOS 1 File
;; 173: Bad Sector
;; 255: FORMATTING Error (DOS 2.5)

;
;
;
;
;  COMMAND CODES FOR CIO
;
;

; Command byte goes in ICCOM,x

;; General-purpose commands:
C_OPEN   = $03    ;    3 OPEN           (BASIC OPEN)
C_GETREC = $05    ;    5 GET RECORD
C_GETCHR = $07    ;    7 GET BYTE
C_PUTREC = $09    ;    9 WRITE RECORD
C_PUTCHR = $0B    ;   11 PUT-BYTE
C_CLOSE  = $0C    ;   12
C_STATUS = $0D    ;   13
C_SPECIL = $0E    ;   14 BEGINNING OF SPECIAL COMMANDS (aka XIO)
;; Commands for S: device:
C_DRAWLN = $11    ;   17 SCREEN DRAW (BASIC DRAWTO)
C_FILLIN = $12    ;   18 SCREEN FILL
;; Commands for D: device (only when DOS is loaded):
C_RENAME = $20    ;   32
C_DELETE = $21    ;   33
C_LOCK   = $23    ;   35
C_UNLOCK = $24    ;   36
C_POINT  = $25    ;   37
C_NOTE   = $26    ;   38

; AUX1 modes (ICAX1,x or 2nd parameter of BASIC OPEN command):
C_OPREAD = $04    ;    4 OPEN FOR INPUT
C_OWRITE = $08    ;    8 OPEN FOR OUTPUT
C_APPEND = $09    ;    9 OPEN TO APPEND TO END OF DISK FILE
C_OUPDAT = $0C    ;   12 OPEN FOR INPUT AND OUTPUT AT THE SAME TIME
;; D: (DOS) only:
C_OPDIR  = $06    ;    6 OPEN TO DISK DIRECTORY
;; S: only:
C_MXDMOD = $10    ;   16 OPEN TO SPLIT SCREEN (MIXED MODE)
C_INSCLR = $20    ;   32 OPEN TO SCREEN BUT DON'T ERASE
;; C: only:
C_NOIRG  = $80    ;  128 NO GAP CASSETTE MODE

;;      Command bytes (ICCOM) for the RS-232 (R:) device:
;;
;;      Output partial block             32      $20
;;      Control RTS,XMT,DTR              34      $22
;;      Baud, stop bits, word size       36      $24
;;      Translation mode                 38      $26
;;      Concurrent mode                  40      $28
;;
;;      (see the 850 Interface Manual for details)


; SIO command bytes (not part of CIO):
S_DFRMAT = $21    ;   33 FORMAT DISK (RESIDENT DISK HANDLER (RDH))
S_PTSECT = $50    ;   80 RDH PUT SECTOR
S_GTSECT = $52    ;   82 RDH GET SECTOR
S_DSTAT  = $53    ;   83 RDH GET STATUS
S_PSECTV = $57    ;   87 RDH PUT SECTOR AND VERIFY
; Various other SIO commands are supported by different drives

; 20061028 bkw: CR/EOL not really part of CIO, but useful:
CR     = $9B    ;  155 CARRIAGE RETURN (EOL)
EOL = CR ; defined in SYSEQU.ASM

;
IOCBSZ = $10    ;   16 IOCB SIZE
MAXIOC = $80    ;  128 MAX IOCB BLOCK SIZE
IOCBF  = $FF    ;  255 IOCB FREE
;
LEDGE  = $02    ;    2 DEFAULT LEFT MARGIN
REDGE  = $27    ;   39 DEFAULT RIGHT MARGIN

;       OS VARIABLES
;
;          PAGE 0
;
LINZBS = $00    ;    0 (800) FOR ORIGINAL DEBUGGER
;        $00        0 (XL) RESERVED
NGFLAG = $01    ;    1 (XL) FOR POWER-UP SELF TEST
CASINI = $02    ;    2
RAMLO  = $04    ;    4 POINTER FOR SELF TEST
TRAMSZ = $06    ;    6 TEMPORARY RAM SIZE
TSTDAT = $07    ;    7 TEST DATA
WARMST = $08    ;    8
BOOTQ  = $09    ;    9 SUCCESSFUL BOOT FLAG
; aka BOOT? in the OS source, but some assemblers don't support ? in labels
DOSVEC = $0A    ;   10 PROGRAM RUN VECTOR
DOSINI = $0C    ;   12 PROGRAM INITIALIZATION
APPMHI = $0E    ;   14 DISPLAY LOW LIMIT
POKMSK = $10    ;   16 IRQ ENABLE FLAGS (shadow for IRQEN)
BRKKEY = $11    ;   17 FLAG
RTCLOK = $12    ;   18 3 BYTES, MSB FIRST
BUFADR = $15    ;   21 INDIRECT BUFFER ADDRESS
ICCOMT = $17    ;   23 COMMAND FOR VECTOR
DSKFMS = $18    ;   24 DISK FILE MANAGER POINTER
DSKUTL = $1A    ;   26 DISK UTILITY POINTER (DUP.SYS)
PTIMOT = $1C    ;   28 (800) PRINTER TIME OUT REGISTER
ABUFPT = $1C    ;   28 (XL) RESERVED
PBPNT  = $1D    ;   29 (800) PRINTER BUFFER POINTER
;        $1D    ;   29 (XL) RESERVED
PBUFSZ = $1E    ;   30 (800) PRINTER BUFFER SIZE
;        $1E    ;   30 (XL) RESERVED
PTEMP  = $1F    ;   31 (800) TEMPORARY REGISTER (PTEMP deleted in XL OS)
;        $1F    ;   31 (XL) RESERVED
ZIOCB  = $20    ;   32 ZERO PAGE IOCB
ICHIDZ = $20    ;   32 HANDLER INDEX NUMBER (ID)
ICDNOZ = $21    ;   33 DEVICE NUMBER
ICCOMZ = $22    ;   34 COMMAND
ICSTAZ = $23    ;   35 STATUS
ICBALZ = $24    ;   36 BUFFER POINTER LOW BYTE
ICBAHZ = $25    ;   37 BUFFER POINTER HIGH BYTE
ICPTLZ = $26    ;   38 PUT ROUTINE POINTER LOW
ICPTHZ = $27    ;   39 PUT ROUTINE POINTER HIGH
ICBLLZ = $28    ;   40 BUFFER LENGTH LOW
ICBLHZ = $29    ;   41
ICAX1Z = $2A    ;   42 AUXILIARY INFORMATION BYTE 1
ICAX2Z = $2B    ;   43
ICSPRZ = $2C    ;   44 TWO SPARE BYTES (CIO USE)
ICIDNO = $2E    ;   46 IOCB NUMBER X 16
CIOCHR = $2F    ;   47 CHARACTER BYTE FOR CURRENT OPERATION
;
STATUS = $30    ;   48 STATUS STORAGE
CHKSUM = $31    ;   49 SUM WITH CARRY ADDED BACK
BUFRLO = $32    ;   50 DATA BUFFER LOW BYTE
BUFRHI = $33    ;   51
BFENLO = $34    ;   52 ADDRESS OF LAST BUFFER BYTE +1 (LOW)
BFENHI = $35    ;   53
CRETRY = $36    ;   54 (800) NUMBER OF COMMAND FRAME RETRIES
XL_LTEMP  = $36    ;   54 (XL) LOADER TEMPORARY STORAGE, 2 BYTES
DRETRY = $37    ;   55 (800) DEVICE RETRIES
BUFRFL = $38    ;   56 BUFFER FULL FLAG
RECVDN = $39    ;   57 RECEIVE DONE FLAG
XMTDON = $3A    ;   58 TRANSMISSION DONE FLAG
CHKSNT = $3B    ;   59 CHECKSUM-SENT FLAG
NOCKSM = $3C    ;   60 CHECKSUM-DOES-NOT-FOLLOW-DATA FLAG
BPTR   = $3D    ;   61
FTYPE  = $3E    ;   62
FEOF   = $3F    ;   63
FREQ   = $40    ;   64
;
SOUNDR = $41    ;   65 0=QUIET I/O
CRITIC = $42    ;   66 CRITICAL FUNCTION FLAG, NO DEFFERED VBI
FMSZPG = $43    ;   67 DOS ZERO PAGE, 7 BYTES
CKEY   = $4A    ;   74 (800) START KEY FLAG
XL_ZCHAIN = $4A    ;   74 (XL) HANDLER LOADER TEMP, 2 BYTES
CASSBT = $4B    ;   75 (800) CASSETTE BOOT FLAG
DSTAT  = $4C    ;   76 DISPLAY STATUS
;
ATRACT = $4D    ;   77
DRKMSK = $4E    ;   78 ATTRACT MASK
COLRSH = $4F    ;   79 ATTRACT COLOR SHIFTER (EORed WITH GRAPHICS)
;
TMPCHR = $50    ;   80
HOLD1  = $51    ;   81
LMARGN = $52    ;   82 SCREEN LEFT MARGIN REGISTER
RMARGN = $53    ;   83 SCREEN RIGHT MARGIN
ROWCRS = $54    ;   84 CURSOR ROW
COLCRS = $55    ;   85 CURSOR COLUMN, 2 BYTES
DINDEX = $57    ;   87 DISPLAY MODE
SAVMSC = $58    ;   88 SCREEN ADDRESS
OLDROW = $5A    ;   90 CURSOR BEFORE DRAW OR FILL
OLDCOL = $5B    ;   91
OLDCHR = $5D    ;   93 DATA UNDER CURSOR
OLDADR = $5E    ;   94 CURSOR ADDRESS
XL_FKDEF  = $60 ;   96 (XL) FUNCTION KEY DEFINITION POINTER (LSB/MSB)
NEWROW = $60    ;   96 (800) DRAWTO DESTINATION
NEWCOL = $61    ;   97 (800) DRAWTO DESTINATION, 2 BYTES
XL_PALNTS = $62 ;   98 (XL) EUROPE/NORTH AMERICA TV FLAG
LOGCOL = $63    ;   99 LOGICAL LINE COLUMN POINTER
MLTTMP = $66    ;  102
OPNTMP = $66    ;  102 TEMPORARY STORAGE FOR CHANNEL OPEN
SAVADR = $68    ;  104
RAMTOP = $6A    ;  106 START OF ROM (END OF RAM + 1), HIGH BYTE ONLY
BUFCNT = $6B    ;  107 BUFFER COUNT
BUFSTR = $6C    ;  108 POINTER USED BY EDITOR
BITMSK = $6E    ;  110 POINTER USED BY EDITOR
SHFAMT = $6F    ;  111
ROWAC  = $70    ;  112
COLAC  = $72    ;  114
ENDPT  = $74    ;  116
DELTAR = $76    ;  118
DELTAC = $77    ;  119
ROWINC = $79    ;  121 (800)
XL_KEYDEF = $79 ;  121 (XL) KEY DEFINITION POINTER, 2 BYTES
COLINC = $7A    ;  122 (800)
SWPFLG = $7B    ;  123 NON 0 IF TEXT AND REGULAR RAM IS SWAPPED
HOLDCH = $7C    ;  124 CH MOVED HERE BEFORE CTRL AND SHIFT
INSDAT = $7D    ;  125 used by S: handler, tmp for char under cursor
COUNTR = $7E    ;  126 used by XIO DRAW command (2 bytes)

; $80 to $FF are free if BASIC and floating point are not used.
; If BASIC is not used, but FP is, $80 to $D0 are still free.
; There is no way to use BASIC without constantly using FP, as all BASIC
; numbers are FP (even "integers" such as line numbers).
ZROFRE = $80    ;  128 FREE ZERO PAGE, 84 BYTES

; BASIC zero page variables:
LOMEM  = $80    ;  128 LSB, BASIC start-of-memory pointer
;        $81    ;  129 MSB, LOMEM (not to be confused with the OS's MEMLO!)
VNTP   = $82    ;  130 LSB, BASIC start of Variable Name Table pointer
;        $83    ;  131 MSB, VNTP
VNTD   = $84    ;  132 LSB, BASIC end of Variable Name Table pointer (+1 byte)
;        $85    ;  133 MSB, VNTP
VVTP   = $86    ;  134 LSB, BASIC start of Variable Value Table pointer
;        $87    ;  135 MSB, VVTP
STMTAB = $88    ;  136 LSB, BASIC start of Statement Table pointer
;        $89    ;  137 MSB, STMTAB
STMCUR = $8A    ;  138 LSB, BASIC current statement pointer
;        $8B    ;  139 MSB, STMCUR
STARP  = $8C    ;  140 LSB, BASIC current string/array table pointer
;        $8D    ;  141 MSB, STARP (also points to end of BASIC program)
RUNSTK = $8E    ;  142 LSB, BASIC runtime stack pointer
;        $8F    ;  143 MSG, RUNSTK
; BASIC and the OS both use the name MEMTOP; I've renamed the BASIC one.
BAS_MEMTOP = $90 ; 144 LSB, pointer to top of BASIC memory
;            $91 ; 145 MSB, BAS_MEMTOP
MEOLFLG = $92   ;  146 "modified EOL flag register", whatever that is
;         $93   ;  147 listed as "spare" by Mapping's Errata
;COX    = $94    ;  148 current output index (?)
POKADR = $95    ;  149 LSB, address of last POKE location
;               ;  150 MSB, POKADR

; Locations $96 to $B5 are used for various purposes by BASIC,
; and most of them are of little or no interest, even for someone
; writing assembly code meant to run as a USR() routine, so I haven't
; bothered listing them all here. See Compute! Books' "Atari BASIC Sourcebook"
; for the gory details. In fact, you can see it here:

; http://users.telenet.be/kim1-6502/6502/absb.html

; It's fascinating (at least it is to me)... includes full source code
; to Atari BASIC!

; DATAD and DATALN are reset to 0 by BASIC RESTORE command.
DATAD  = $B6    ;  182 the data element being read (e.g. 10 for 10th item
                ;      in a DATA statement)
DATALN = $B7    ;  183 LSB current DATA statement line number
;        $B8    ;  184 MSB, DATALN
;ERRNUM = $B9   ;  185 Most recent error number. Gets cleared before you
                ;      can PEEK it; use ERRSAVE instead.
STOPLN = $BA    ;  186 LSB, line where a program stopped by STOP/break/error
;        $BB    ;  187 MSB, STOPLN
; what are $BC and $BD for?
SAVCUR = $BE    ;  190 Saves the current line address (LSB?)
;        $BF    ;  191 presumably, the MSB of SAVCUR?
IOCMD  = $C0    ;  192, I/O Command (Mapping Errata)
IODVC  = $C1    ;  193, I/O Device (Mapping Errata)
PROMPT = $C2    ;  194, Prompt character (Mapping Errata, presumably INPUT?)
ERRSAVE = $C3   ;  195 Error code that caused a stop or TRAP
;TEMPA  = $C4   ;  196 a 2-byte temp
;ZTEMP2 = $C6   ;  198 a 2-byte temp
COLOR  =  $C8   ;  200 Stores color from COLOR command
PTABW  =  $C9   ;  201 Number of columns between tab stops
                ;      (for PRINT with commas, not the TAB key)
LOADFLG = $CA   ;  202 Load in progress flag. I can tell you from bitter
                ;      experience that BASIC clears this often.

; $CB - $CF are unused by BASIC or the ASM/ED cart.
; $D0 and $D1 are unused by BASIC (does that mean they *are* used by ASM/ED?)

; $D2 and $D3 are used by BASIC. Mapping Errata calls them the "BASIC
; floating-point work area". They get cleared to 0 by BASIC, probably
; every time a FP number is used (e.g. "POKE 210,1:? PEEK(210)" prints 0).
; The BASIC source code labels $D2 as TVTYPE and VTYPE, and $D3 as
; TVNUM and VNUM.

; Floating point zero page variables:
FPZRO  = $D4    ;  212 FLOATING POINT RAM, 43 BYTES
                ;  (20070530 bkw: pretty sure that comment is wrong, and
                ;  should read 44 bytes; see $FF below)
FR0    = $D4    ;  212 FP REGISTER 0 (also used by BASIC for USR() return val)
                ;  (FR0/FRE/FR1/FR2 are each 6 bytes long)
FRE    = $DA    ;  218
FR1    = $E0    ;  224 FP REGISTER 1
FR2    = $E6    ;  230 FP REGISTER 2
FRX    = $EC    ;  236 SPARE
EEXP   = $ED    ;  237 VALUE OF E
NSIGN  = $ED    ;  237 SIGN OF FP NUMBER
ESIGN  = $EF    ;  239 SIGN OF FP EXPONENT
FCHFLG = $F0    ;  240 FIRST CHARACTER FLAG
DIGRT  = $F1    ;  241 NUMBER OF DIGITS RIGHT OF DECIMAL POINT
CIX    = $F2    ;  242 INPUT INDEX
INBUFF = $F3    ;  243 POINTER TO ASCII FP NUMBER
ZTEMP1 = $F5    ;  245
ZTEMP4 = $F7    ;  247
ZTEMP3 = $F9    ;  249
DEGFLG = $FB    ;  251
RADFLG = $FB    ;  251 0=RADIANS, 6=DEGREES
FLPTR  = $FC    ;  252 POINTER TO BCD FP NUMBER (2 bytes)
FPTR2  = $FE    ;  254 maybe a 2nd pointer to an FP number? (2 bytes)
;        $FF    ;  255 This *definitely* is used by the FP package
                ;      Try: POKE 255,0:? SIN(1):? PEEK(255)

;
;          PAGE 1
;
;        65O2 STACK
;
;

;
;
;          PAGE 2
;
;
; 20070529 bkw: Bytes listed as "spare" should NOT be used for your own
; purposes. They may not really be unused (just undocumented), and/or they
; may be unused on the 800 but not the XL (or vice versa).
INTABS = $0200  ;  512 INTERRUPT RAM
VDSLST = $0200  ;  512 NMI VECTOR
VPRCED = $0202  ;  514 PROCEED LINE IRQ VECTOR
VINTER = $0204  ;  516 INTERRUPT LINE IRQ VECTOR
VBREAK = $0206  ;  518 break key IRQ vector (not in OS rev. A)
VKEYBD = $0208  ;  520 keyboard IRQ vector (not break/console keys)
VSERIN = $020A  ;  522 SERIAL INPUT READY IRQ
VSEROR = $020C  ;  524 SERIAL OUTPUT READY IRQ
VSEROC = $020E  ;  526 SERIAL OUTPUT COMPLETE IRQ
VTIMR1 = $0210  ;  528 TIMER 1 IRQ vector
VTIMR2 = $0212  ;  530 TIMER 2 IRQ vector
VTIMR4 = $0214  ;  532 TIMER 4 IRQ vector
VIMIRQ = $0216  ;  534 IRQ VECTOR
CDTMV1 = $0218  ;  536 COUNTDOWN TIMER 1 vector
CDTMV2 = $021A  ;  538 COUNTDOWN TIMER 2 vector
CDTMV3 = $021C  ;  540 COUNTDOWN TIMER 3 vector
CDTMV4 = $021E  ;  542 COUNTDOWN TIMER 4 vector
CDTMV5 = $0220  ;  544 COUNTDOWN TIMER 5 vector
VVBLKI = $0222  ;  546 immediate VBLANK vector
VVBLKD = $0224  ;  548 deferred VBLANK vector (ignore if CRITIC != 0)
CDTMA1 = $0226  ;  550 COUNTDOWN TIMER 1 JSR ADDRESS
CDTMA2 = $0228  ;  552 COUNTDOWN TIMER 2 JSR ADDRESS
CDTMF3 = $022A  ;  554 COUNTDOWN TIMER 3 FLAG
SRTIMR = $022B  ;  555 REPEAT TIMER
CDTMF4 = $022C  ;  556 COUNTDOWN TIMER 4 FLAG
INTEMP = $022D  ;  557 IAN'S TEMP (used by SETVBL routine)
CDTMF5 = $022E  ;  558 COUNTDOWN TIMER FLAG 5
SDMCTL = $022F  ;  559 DMACTL SHADOW
SDLSTL = $0230  ;  560 DISPLAY LIST POINTER, LSB (shadow for DLISTL)
SDLSTH = $0231  ;  561 display list pointer, MSB (shadow for DLISTH)
SSKCTL = $0232  ;  562 SKCTL SHADOW
;        $0233  ;  563 (800) UNLISTED (Mapping calls this SPARE)
XL_LCOUNT = $0233  ;  563 (XL) LOADER TEMP
LPENH  = $0234  ;  564 LIGHT PEN HORIZONTAL (shadow for PENH)
LPENV  = $0235  ;  565 LIGHT PEN VERTICAL (shadow for PENV)
;        $0236  ;  566 2 SPARE BYTES on OS rev A
VBRKKY = $0236 ;  566 Break key interrupt vector (OS rev B and XL)
BRKKY  = VBRKKY ; "OS rev 5" listing calls it this
;        $0238  ;  568 (800) SPARE, 2 BYTES
;XL_RELADR = $0238  ;  568 (XL) relocatable loader relative addr, 1200XL only!
XL_VPIRQ  = $0238  ;  568 (XL) PBI IRQ vector (not on 1200XL!)
CDEVIC = $023A  ;  570 DEVICE COMMAND FRAME BUFFER
CAUX1  = $023C  ;  572 DEVICE COMMAND AUX 1
CAUX2  = $023D  ;  573 DEVICE COMMAND AUX 2
TEMP   = $023E  ;  574 TEMPORARY STORAGE
ERRFLG = $023F  ;  575 DEVICE ERROR FLAG (EXCEPT TIMEOUT)
DFLAGS = $0240  ;  576 FLAGS FROM DISK SECTOR 1
DBSECT = $0241  ;  577 NUMBER OF BOOT DISK SECTORS
BOOTAD = $0242  ;  578 BOOT LOAD ADDRESS POINTER
COLDST = $0244  ;  580 COLD START FLAG, 1 = COLD START IN PROGRESS
;        $0245  ;  581 (800) SPARE
XL_RECLEN = $0245  ;  581 (XL) LOADER
DSKTIM = $0246  ;  582 (800) DISK TIME OUT REGISTER
;        $0246  ;  582 (XL) RESERVED, 39 BYTES
LINBUF = $0247  ;  583 (800) CHARACTER LINE BUFFER, 40 BYTES
                ;  LINBUF was deleted from the XL OS and replaced with:

; $0247 - $024D are "reserved" on the 1200XL. On other XL's they are:
XL_PDVMSK = $0247 ; 583 shadow for PBI device selection register @ $D1FF
XL_SHPDVS = $0248 ; 584 shadow for PBI register (where??)
XL_PDMSK  = $0249 ; 585 PBI interrupt mask
XL_RELADR = $024A ; 586 (XL) LSB, relocatable loader relative addr (NOT 1200XL)
;           $024B ; 587 MSB, XL_RELADR
XL_PPTMPA = $024C ; 588 temporaries for relocatable loader
XL_PPTMPX = $024D ; 589 "

; $024E - $026A are "spare" on all XL/XE's

; More XL stuff:
XL_CHSALT = $026B  ;  619 (XL) CHARACTER SET POINTER (ctrl-F4 on 1200XL)
XL_VSFLAG = $026C  ;  620 (XL) FINE SCROLL TEMPORARY
XL_KEYDIS = $026D  ;  621 (XL) KEYBOARD DISABLE (ctrl-F1 on 1200XL)
XL_FINE   = $026E  ;  622 (XL) FINE SCROLL FLAG (POKE 622,255:GR.0)

GPRIOR = $026F  ;  623 P/M PRIORITY AND GTIA MODES (shadow for PRIOR)
;GTIA   = $026F ;  623 ; 20070529 bkw: does anyone define this?

; Game controller shadows (joysticks/paddles)
; Joystick directions and paddle triggers (buttons) are wired to the PIA.
; Joystick triggers (fire buttons) and the actual paddle potentiometers
; are wired to the GTIA.
; If this seems a little odd, that's because it is :)

; Paddles (potentiometers):
PADDL0 = $0270  ;  624 (XL) 3 MORE PADDLES, (800) 7 MORE PADDLES
PADDL1 = $0271  ;  625 (these are read in BASIC with PADDLE(x)
PADDL2 = $0272  ;  626 (PADDL0-7 are shadows for POT0-7)
PADDL3 = $0273  ;  627
PADDL4 = $0274  ;  628 (PADDL4-7 are copies of PADDL0-3 on the XL)
PADDL5 = $0275  ;  629
PADDL6 = $0276  ;  630
PADDL7 = $0277  ;  631

; Joysticks (directions only)
STICK0 = $0278  ;  632 (XL) 1 MORE STICK, (800) 3 MORE STICKS
STICK1 = $0279  ;  633 (these are read in BASIC with STICK(x)
STICK2 = $027A  ;  634 (STICK0/1 are shadows for PORTA; STICK2/3 shadows PORTB)
STICK3 = $027B  ;  635
; STICK0 is a shadow for bits 4-7 of PORTA (shifted 4 bits right)
; STICK1 is a shadow for bits 0-3 of PORTA

; On the 800:
; STICK2 is a shadow for bits 4-7 of PORTB (shifted 4 bits right)
; STICK3 is a shadow for bits 0-3 of PORTB

; On the XL/XE series:
; STICK2 and STICK3 are copies of STICK0 and STICK1, respectively.

; In the XL/XE machines, there are only 2 joystick ports, and PORTB
; (formerly joystick ports) is now used to control the MMU.

; joystick directions are active low (1=not pressed) and decode as:

; bit        direction
; 0 or 4     up
; 1 or 5     down
; 2 or 6     left
; 3 or 7     right

; A value of $0F in a STICKx register means no direction is being pressed.
; When a direction is pressed, its bit becomes a logic 0, so e.g. $0E means
; someone's moving the joystick up.

; (bits 4-7 are only used when reading directly from the HW registers,
;  PORTA and PORTB).

; Paddle triggers (buttons)
PTRIG0 = $027C  ;  636 (XL) 3 MORE PADDLE TRIGGERS, (800) 7 MORE
PTRIG1 = $027D  ;  637 (these are read in BASIC with PTRIG(x))
PTRIG2 = $027E  ;  638 (PTRIG0-3 are shadows for PORTA)
PTRIG3 = $027F  ;  639
PTRIG4 = $0280  ;  640 (PTRIG4-7 are shadows for PORTB on the 800)
PTRIG5 = $0281  ;  641 (they are copies of PTRIG0-3 on the XL)
PTRIG6 = $0282  ;  642
PTRIG7 = $0283  ;  643
; In case someone doesn't already know this: The paddle triggers are wired
; to the same pins on the joystick port as the left/right joystick directions.
; Each pair of paddles uses left for the first paddle's trigger and right
; for the second (so PTRIG0/1 are also the left/right bits in STICK0,
; PTRIG2/3 are STICK1, etc).

; Joystick triggers (buttons)
STRIG0 = $0284  ;  644 (XL) 1 MORE STICK TRIGGER, (800) 3 MORE
STRIG1 = $0285  ;  645 (these are read in BASIC with STRIG(x))
STRIG2 = $0286  ;  646 (STRIG0-3 are shadows for TRIG0-3)
STRIG3 = $0287  ;  647

; C: handler variables:
CSTAT  = $0288  ;  648 (800) Cassette status register
; note that CSTAT was deleted from the XL OS, and replaced with:
XL_HIBYTE = $0288 ; 648 (XL) used by relocatable loader
WMODE  = $0289  ;  649 used by C: handler (0=read, 128-write)
BLIM   = $028A  ;  650 cassette buffer data record size
;        $028B  ;  651 (800) 5 SPARE BYTES (to $028F)
XL_IMASK  = $028B  ;  651 (XL)  used by relocatable loader
XL_JVECK  = $028C  ;  652 (XL)  (Mapping says it's unused)
                   ;  653 (XL)  Presumably the MSB of JVECK (unused?)
XL_NEWADR = $028E  ;  654 (XL)  LOADER RAM (2 bytes)

; Misc. S: and/or E: handler variables:
TXTROW = $0290  ;  656
TXTCOL = $0291  ;  657
TINDEX = $0293  ;  659 TEXT INDEX
TXTMSC = $0294  ;  660
TXTOLD = $0296  ;  662 OLD ROW AND OLD COL FOR TEXT, 2 BYTES
;        $0298  ;  664 4 SPARE BYTES
TMPX1  = $029C  ;  668 (800)
; note that TMPX1 was deleted from the XL OS, and replaced with:
XL_CRETRY = $029C  ;  668 (XL)  NUMBER OF COMMAND FRAME RETRIES
                   ; (moved from CRETRY on 800)
SUBTMP = $029E  ;  670
HOLD2  = $029F  ;  671
DMASK  = $02A0  ;  672
TMPLBT = $02A1  ;  673
ESCFLG = $02A2  ;  674
TABMAP = $02A3  ;  675 15 BYTE BIT MAP FOR TAB SETTINGS
LOGMAP = $02B2  ;  690 4 BYTE LOGICAL LINE START BIT MAP
INVFLG = $02B6  ;  694 mask for inverse video ($80=inverse, 0=normal)
FILFLG = $02B7  ;  695 FILL DURING DRAW FLAG
TMPROW = $02B8  ;  696
TMPCOL = $02B9  ;  697
SCRFLG = $02BB  ;  699 SCROLL FLAG
HOLD4  = $02BC  ;  700
HOLD5  = $02BD  ;  701 (800)
; note that HOLD5 was deleted from the XL OS, and replaced with:
XL_DRETRY = $02BD  ;  701 (XL)  NUMBER OF DEVICE RETRIES
                   ; (moved from DRETRY on 800)
SHFLOC = $02BE  ;  702
BOTSCR = $02BF  ;  703 24 NORM, 4 SPLIT

; Color register shadows (HW registers are in GTIA)
PCOLR0 = $02C0  ;  704 3 MORE PLAYER COLOR REGISTERS (shadows for COLPM0-3)
PCOLR1 = $02C1  ;  705 (missiles use same color regs as same-numbered players!)
PCOLR2 = $02C2  ;  706
PCOLR3 = $02C3  ;  707
COLOR0 = $02C4  ;  708 4 MORE GRAPHICS COLOR REGISTERS (shadows for COLPF0-3)
COLOR1 = $02C5  ;  709 (text luminance in GR.0)
COLOR2 = $02C6  ;  710 (text background and chroma in GR.0)
COLOR3 = $02C7  ;  711
COLOR4 = $02C8  ;  712 (background, shadow for COLBK)
; On boot, system reset, or any time S:/E: devices are opened:
; PCOLR0-3 are initialzed to 0 ($00, black)
; COLOR0 is initialized to  40 ($28, orange)
; COLOR1 is initialized to 202 ($CA, green)
; COLOR2 is initialized to 148 ($94, blue)
; COLOR3 is initialized to  70 ($46, red)
; COLOR4 is initialized to   0 ($00, black)

;        $02C9    713 (800) 23 SPARE BYTES
; XL relocatable handler and other variables:
XL_RUNADR = $02C9  ;  713 (XL) LOADER VECTOR
XL_HIUSED = $02CB  ;  715 (XL) LOADER VECTOR
XL_ZHIUSE = $02CD  ;  717 (XL) LOADER VECTOR
XL_GBYTEA = $02CF  ;  719 (XL) LOADER VECTOR
XL_LOADAD = $02D1  ;  721 (XL) LOADER VECTOR
XL_ZLOADA = $02D3  ;  723 (XL) LOADER VECTOR
XL_DSCTLN = $02D5  ;  725 (XL) DISK SECTOR SIZ
XL_ACMISR = $02D7  ;  727 (XL) RESERVED
XL_KRPDER = $02D9  ;  729 (XL) KEY AUTO REPEAT DELAY
XL_KEYREP = $02DA  ;  730 (XL) KEY AUTO REPEAT RATE
XL_NOCLIK = $02DB  ;  731 (XL) KEY CLICK DISABLE (ctrl-F3 on 1200XL)
XL_HELPFG = $02DC  ;  732 (XL) HELP KEY FLAG
XL_DMASAV = $02DD  ;  733 (XL) SDMCTL (DMA) SAVE (ctrl-F2 on 1200XL)
XL_PBPNT  = $02DE  ;  734 (XL) PRINTER BUFFER POINTER (moved from PBPNT on 800)
XL_PBUFSZ = $02DF  ;  735 (XL) PRINTER BUFFER SIZE (moved from PBUFSZ on 800)
; note that PTEMP was deleted from the XL OS

; DOS/FMS variables:
GLBABS = $02E0  ;  736 GLOBAL VARIABLES, 4 SPARE BYTES (if DOS not loaded)
                ;  If DOS/FMS is loaded:
RUNAD  = $02E0  ;  736 (DOS) Run address for binary file (LSB/MSB)
INITAD = $02E2  ;  736 (DOS) Init address for binary file (LSB/MSB)

; SYSEQU.ASM defines these:
GOADR = RUNAD
INITADR = INITAD

; OS variables:
RAMSIZ = $02E4  ;  740 PERMANENT START OF ROM POINTER
MEMTOP = $02E5  ;  741 END OF FREE RAM
MEMLO  = $02E7  ;  743 LSB, points to bottom of free memory ($0700 if DOS
                ;      not booted). Not to be confused with BASIC's LOMEM!
;        $02E8  ;  744 MSB of MEMLO

;        $02E9  ;  745 (800) SPARE
XL_HNDLOD = $02E9  ;  745 (XL) HANDLER LOADER FLAG

DVSTAT = $02EA  ;  746 DEVICE STATUS BUFFER, 4 BYTES
CBAUDL = $02EE  ;  750 CASSETTE BAUD RATE, 2 BYTES
CRSINH = $02F0  ;  752 1 = INHIBIT CURSOR
KEYDEL = $02F1  ;  753 KEY DELAY AND RATE (aka debounce counter)
CH1    = $02F2  ;  754 prior keyboard character code
CHACT  = $02F3  ;  755 (shadow for CHACTL)
CHBAS  = $02F4  ;  756 CHARACTER SET POINTER (shadow for CHBASE)

; These next 4 are located elsewhere on the 800 OS:
XL_NEWROW = $02F5  ;  757 (XL) DRAW DESTINATION
XL_NEWCOL = $02F6  ;  758 (XL) DRAW DESTINATION
XL_ROWINC = $02F8  ;  760 (XL)
XL_COLINC = $02F9  ;  761 (XL)
; $02F5 - $02F9 are "spare" on the 800.

CHAR   = $02FA  ;  762 most recent character read/written (screen code)
ATACHR = $02FB  ;  763 ATASCII CHARACTER FOR CIO
CH     = $02FC  ;  764 last key pressed (internal scan code)
FILDAT = $02FC  ;  764 COLOR FOR SCREEN FILL
DSPFLG = $02FE  ;  766 DISPLAY CONTROL CHARACTERS FLAG
SSFLAG = $02FF  ;  767 DISPLAY START/STOP FLAFG

;
;           PAGE 3
;
;
;     RESIDENT DISK HANDLER/SIO INTERFACE
;
; The DCB is used for SIO (serial I/O).
DCB    = $0300  ;  768 DEVICE CONTROL BLOCK
DDEVIC = $0300  ;  768 device ID ($31-$38 for D1:-D8:)
DUNIT  = $0301  ;  769 disk/device unit numder
DCOMND = $0302  ;  770 device command
DSTATS = $0303  ;  771 status code (set by OS)
DBUFLO = $0304  ;  772 data buffer LSB (set by user)
DBUFHI = $0305  ;  773 data buffer MSB (set by user)
DTIMLO = $0306  ;  774 timeout (set by user, units of 60/64 seconds)
DUNUSE = $0307  ;  775 unused
DBYTLO = $0308  ;  776 number of bytes to transfer, LSB
DBYTHI = $0309  ;  777 number of bytes to transfer, MSB
DAUX1  = $030A  ;  778 LSB of sector number (for disk) (set by user)
DAUX2  = $030B  ;  779 MSB of sector number (for disk)
TIMER1 = $030C  ;  780 INITIAL TIMER VALUE
ADDCOR = $030E  ;  782 (800) ADDITION CORRECTION
; note that ADDCOR was deleted from the XL OS, and replaced with:
XL_JMPERS = $030E  ;  782 (XL) OPTION JUMPERS
CASFLG = $030F  ;  783 CASSETTE MODE WHEN SET
TIMER2 = $0310  ;  784 FINAL VALUE, TIMERS 1 & 2 DETERMINE BAUD RATE
TEMP1  = $0312  ;  786
XL_TEMP2  = $0313  ;  787 (XL)
TEMP2  = $0314  ;  788 (800)
XL_PTIMOT = $0314  ;  788 (XL) PRINTER TIME OUT
TEMP3  = $0315  ;  789
SAVIO  = $0316  ;  790 SAVE SERIAL IN DATA PORT
TIMFLG = $0317  ;  791 TIME OUT FLAG FOR BAUD RATE CORRECTION
STACKP = $0318  ;  792 SIO STACK POINTER SAVE
TSTAT  = $0319  ;  793 TEMPORARY STATUS HOLDER
HATABS = $031A  ;  794 HANDLER ADDRESS TABLE, 38 BYTES
MAXDEV = $0321  ;  801 MAXIMUM HANDLER ADDRESS INDEX
XL_PUPBT1 = $033D  ;  829 (XL) POWER-UP/RESET
XL_PUPBT2 = $033E  ;  830 (XL) POWER-UP/RESET
XL_PUPBT3 = $033F  ;  831 (XL) POWER-UP/RESET

; IOCB's, 8 of them, 16 bytes each.
; Set X register to (IOCB number * 16), and use e.g. ICCOM,x
;
IOCB   = $0340  ;  832 ; IOCB base address
ICHID  = $0340  ;  832 ; Handler ID (set by OS)
ICDNO  = $0341  ;  833 ; Device number (set by OS)
ICCOM  = $0342  ;  834 ; Command byte (see C_* constants) (set by user)
ICCMD  = ICCOM  ;      ; alternate name for ICCOM, according to Mapping.
ICSTA  = $0343  ;  835 ; Status (set by OS)
ICBAL  = $0344  ;  836 ; Buffer address, LSB (set by user)
ICBAH  = $0345  ;  837 ; Buffer address, MSB (set by user)
ICPTL  = $0346  ;  838 ; Put-one-byte address minus one, LSB (set by OS)
ICPTH  = $0347  ;  839 ; Put-one-byte address minus one, MSB (set by OS)
ICBLL  = $0348  ;  840 ; Buffer length, LSB (set by user)
ICBLH  = $0349  ;  841 ; Buffer length, MSB (set by user)
ICAX1  = $034A  ;  842 ; AUX1 byte (2nd param in BASIC OPEN) (set by user)
ICAX2  = $034B  ;  843 ; AUX2 byte (4rd param in BASIC OPEN) (set by user)
ICAX3  = $034C  ;  844 ; AUX3 byte (used by NOTE/POINT) (set by user)
ICAX4  = $034D  ;  845 ; AUX4 byte (used by NOTE/POINT) (set by user)
ICAX5  = $034E  ;  846 ; AUX5 byte (used by NOTE/POINT) (set by user)
ICAX6  = $034F  ;  847 ; Spare aux byte
;                     OTHER IOCB's, 112 BYTES ($300 + $10 * channel)

IOCBLEN = ICAX6-IOCB+1 ; length of one IOCB (from SYSEQU.ASM)

; Alternative names for the above. I found these in SYSEQU.ASM, as
; distributed with the disk version of Mac65.
ICBADR = ICBAL
ICPUT  = ICPTL
ICBLEN = ICBLL
ICAUX1 = ICAX1
ICAUX2 = ICAX2
ICAUX3 = ICAX3
ICAUX4 = ICAX4
ICAUX5 = ICAX5
ICAUX6 = ICAX6

PRNBUF = $03C0  ;  960 PRINTER BUFFER, 40 BYTES
;        $03E8  ; 1000 (800) 21 SPARE BYTES
XL_SUPERF = $03E8  ; 1000 (XL) SCREEN EDITOR
XL_CKEY   = $03E9  ; 1001 (XL) START KEY FLAG
XL_CASSBT = $03EA  ; 1002 (XL) CASSETTE BOOT FLAG
XL_CARTCK = $03EB  ; 1003 (XL) CARTRIDGE CHECKSUM
XL_ACMVAR = $03ED  ; 1005 (XL) RESERVED, 10 BYTES (to $03F7)
XL_BASICF = $03F8  ; 1006 (XL) 0 if ROM-BASIC enabled, 1 if not
XL_MINTLK = $03F9  ; 1017 (XL) RESERVED
XL_GINTLK = $03FA  ; 1018 (XL) CARTRIDGE INTERLOCK
XL_CHLINK = $03FB  ; 1019 (XL) HANDLER CHAIN, 2 BYTES
CASBUF = $03FD  ; 1021 CASSETTE BUFFER, 131 BYTES TO $047F

; Layout of the cassette buffer after a cassette block is read:

; Baud correction ($55 $55) bytes are located at offsets 0 and 1
; Control byte is at offset 2 ($03FF):
; Actual data (128 bytes) runs from offset 3 ($0400) to $047F.
; Each cassette frame has a 1 byte checksum after the 128 data bytes, but
; the checksum is NOT stored anywhere in the cassette buffer!

; CONTROL BYTE VALUES
; Value      Meaning
; 250 ($FA)  Partial record follows. The actual number of bytes is stored
;            in the last byte of the record (CASBUF+130, or $047F).
; 252 ($FC)  Record full; 128 bytes follow.
; 254 ($FE)  End of File (EOF) record; followed by 128 zero bytes.

; Boot tapes normally don't have partial or EOF records, but BASIC
; CLOAD/LOAD/LIST and data file tapes do.

; Mapping the Atari says the first disk boot sector is read into CASBUF also.

;
;
;            PAGE 4
;
;
USAREA = $0480  ; 1152 128 SPARE BYTES (but used by BASIC)
;
;  SEE APPENDIX C FOR PAGES 4 AND 5 USAGE

;
;
;
;
;          PAGE 5
;
PAGE5  = $0500  ; 1280 127 FREE BYTES
;        $057E   1406 129 FREE BYTES IF FLOATING POINT ROUTINES NOT USED
;
;FLOATING POINT NON-ZERO PAGE RAM, NEEDED ONLY IF FP IS USED
; (20070529 bkw: BASIC constantly uses FP! Also, it uses some of these
; addresses for its own purposes.)
;
LBPR1  = $057E  ; 1406 LBUFF PREFIX 1
LBPR2  = $05FE  ; 1534 LBUFF PREFIX 2
LBUFF  = $0580  ; 1408 LINE BUFFER
PLYARG = $05E0  ; 1504 POLYNOMIAL ARGUMENTS
FPSCR  = $05E6  ; 1510 PLYARG+FPREC
FPSCR1 = $05EC  ; 1516 FPSCR+FPREC
FSCR   = $05E6  ; 1510 =FPSCR
FSCR1  = $05EC  ; 1516 =FPSCR1
LBFEND = $05FF  ; 1535 END OF LBUFF

;
;           PAGE 6
;
;
PAGE6  = $0600  ; 1536 256 FREE BYTES

;
;
;           PAGE 7
;
;
BOOTRG = $0700  ; 1792 PROGRAM AREA
; Boot disks (including DOS) are generally loaded here. Also, BASIC RAM
; (variables and program) starts here, if BASIC is booted without DOS.

; Page 80 (XL): Self-test (aka diagnostic) ROM is mapped at $5000,
; if enabled with bit 7 of PORTB. Normally only happens if you boot without
; BASIC, cartridge, tape, or disk... or if the OS detects a memory error
; during boot.

;
;
;       UPPER ADDRESSES
;
;
RITCAR = $8000  ;32768 RAM IF NO CARTRIDGE (extends to $9FFF)
LFTCAR = $A000  ;40960 RAM IF NO CARTRIDGE (extends to $BFFF)

; These 2 are from the Atari System Reference Manual, chapter 12:
CARTA  = LFTCAR
CARTB  = RITCAR

CARTLOC = $BFFA ;49146 cartridge run address (from SYSEQU.ASM)

; Carts were originally 8K only when the 400/800 were first released.
; There were plans to release 16K programs on two cartridges, but this
; never happened (the price of 16K ROMs came down, I guess). 16K cartridges
; go in the left slot, but they actually use the address space for both
; the right and left slots.

; Mapping the Atari has this to say about cartridges:
;; Byte                                     Purpose
;; Left (A)                     Right(B)
;; 49146 ($BFFA)             40954 ($9FFA)  Cartridge start address (low byte)
;;
;; 49147 ($BFFB)             40955 ($9FFB)  Cartridge start address (high byte)
;;
;; 49148 ($BFFC)             40956 ($9FFC)  Reads zero if a cartridge is
;;      inserted, non-zero when no cartridge is present. This information
;;      is passed down to the page zero RAM: if the A cartridge is plugged
;;      in, then location 6 will read one; if the B cartridge is plugged in,
;;      then location 7 will read one; otherwise they will read zero.
;;
;; 49149 ($BFFD)             40957 ($9FFD)  Option byte. If BIT 0 equals one,
;;      then boot the disk (else there is no disk boot). If BIT 2 equals one,
;;      then initialize and start the cartridge (else initialize but do not
;;      start). If BIT 7 equals one, then the cartridge is a diagnostic
;;      cartridge which will take control, but not initialize the OS (else
;;      non-diagnostic cartridge). Diagnostic cartridges were used by
;;      Atari in the development of the system and are not available to the
;;      public.
;;
;; 49150 ($BFFE)             40958 ($9FFE)  Cartridge initialization address
;;      low byte.
;;
;; 49151 ($BFFF)             40959 ($9FFF)  Cartridge initialization address
;;      high byte. This is the address to which the OS will jump during all
;;      powerup and RESETs.
;;
;;      The OS makes temporary use of locations 36876 to 36896 ($900C to
;;      $9020) to set up vectors for the interrupt handler. See the OS
;;      listings pages 31 and 81. This code was only used in the
;;      development system used to design the Atari.


; Page 192

C0PAGE = $C000  ;49152 (800) EMPTY, 4K BYTES
                ; 20070529 bkw: unmapped address space.
                ; Mapping the Atari erroneously lists this as "unused ROM".
                ; There are upgrades to the 800 to give 4K of RAM here
                ; (for a total of 52K of RAM), or ROM (Omnimon?).
                ; Also, there is RAM here if you boot the Translator
                ; disk on an XL.

; (XL) $C000 also contains info about the ROM revision. From Mapping:

;Bytes 49152-49163 ($C000-$C00B) are used to identify the computer
;and the ROM in the $C000-$DFFF block:
;
;Byte             Use
;49152-3/C000-1   Checksum (LSB/MSB) of all the bytes
;                 in ROM except the checksum bytes
;                 themselves.
;49154/C002       Revision date, stored in the form
;                 DDMMYY. This is DD, day, usually $10.
;49155/C003       Revision date, month; usually $05.
;49156/C004       Revision date, year; usually $83.
;49157/C005       Reserved option byte; reads zero for
;                 the 1200, 800XL, and 130XE.
;49158/C006       Part number in the form AANNNNNN;
;                 AA is an ASCII character and
;                 NNNNNN is a four-bit BCD digit. This is
;                 byte A1.
;49159-62/C007-A  Part number, bytes A2, N1-N6 (each
;                 byte has two N values of four bits
;                 each).
;49163/C00B       Revision number. Mapping author's 800XL and 130XE say 2.

;C0PAGE = $C000  ;49152 (XL) OS ROM, mostly interrupt handlers
;        $C800  51200 (XL) START OF OS ROM
CHORG2 = $CC00  ;52224 (XL) INTERNATIONAL CHARACTER SET



;
;
;      HARDWARE REGISTERS
;
;
;  SEE REGISTER LIST FOR MORE INFORMATION
;
;

; GTIA
GTIA   = $D000
HPOSP0 = $D000  ;53248 (W) ; P/M positions (no shadows)
HPOSP1 = $D001  ;53249 (W)
HPOSP2 = $D002  ;53250 (W)
HPOSP3 = $D003  ;53251 (W)
HPOSM0 = $D004  ;53252 (W)
HPOSM1 = $D005  ;53253 (W)
HPOSM2 = $D006  ;53254 (W)
HPOSM3 = $D007  ;53255 (W)
SIZEP0 = $D008  ;53256 (W) ; P/M size regs (no shadows)
SIZEP1 = $D009  ;53257 (W)
SIZEP2 = $D00A  ;53258 (W)
SIZEP3 = $D00B  ;53259 (W)
SIZEM  = $D00C  ;53260 (W)
M0PF   = $D000  ;53248 (R) ; collision regs (no shadows)
M1PF   = $D001  ;53249 (R)
M2PF   = $D002  ;53250 (R)
M3PF   = $D003  ;53251 (R)
P0PF   = $D004  ;53252 (R)
P1PF   = $D005  ;53253 (R)
P2PF   = $D006  ;53254 (R)
P3PF   = $D007  ;53255 (R)
M0PL   = $D008  ;53256 (R)
M1PL   = $D009  ;53257 (R)
M2PL   = $D00A  ;53258 (R)
M3PL   = $D00B  ;53259 (R)
P0PL   = $D00C  ;53260 (R)
P1PL   = $D00D  ;53261 (R)
P2PL   = $D00E  ;53262 (R)
P3PL   = $D00F  ;53263 (R)
GRAFP0 = $D00D  ;53261 (W) ; direct (non-DMA) P/M graphics regs (no shadows)
GRAFP1 = $D00E  ;53262 (W)
GRAFP2 = $D00F  ;53263 (W)
GRAFP3 = $D010  ;53264 (W)
GRAFM  = $D011  ;53265 (W)
TRIG0  = $D010  ;53264 (R) ; Joystick triggers (shadows @ STRIG0-3)
TRIG1  = $D011  ;53265 (R)
TRIG2  = $D012  ;53266 (R)
TRIG3  = $D013  ;53267 (R)
PAL    = $D014  ;53268 (R) ; PAL/NTSC detect (no shadow)
                ; PAL supposedly moved to XL_PALNTS on XL; what was it
                ; replaced with?
COLPM0 = $D012  ;53266 (W) ; P/M colors (shadows @ PCOLR0-3)
COLPM1 = $D013  ;53267 (W)
COLPM2 = $D014  ;53268 (W)
COLPM3 = $D015  ;53269 (W)
COLPF0 = $D016  ;53270 (W) ; Playfield colors (shadows @ COLOR0-3)
COLPF1 = $D017  ;53271 (W)
COLPF2 = $D018  ;53272 (W)
COLPF3 = $D019  ;53273 (W)
COLBK  = $D01A  ;53274 (W) ; Background color (shadow @ COLOR4)
PRIOR  = $D01B  ;53275 (W) ; GTIA priority (shadow @ GPRIOR)
GTIAR  = $D01B  ;53275 (R?)
VDELAY = $D01C  ;53276 (W)
GRACTL = $D01D  ;53277 (W)
HITCLR = $D01E  ;53278 (W), latch
CONSOL = $D01F  ;53279 (W=keyclick spkr, R=console keys)

; $D020 - $D0FF are mirrors of GTIA address space
; $D100 - $D1FF are supposed to be unused (unmapped) on the 800
; On the XL, $D100 - $D1FF is switched to device memory during PBI I/O

; POKEY
POKEY  = $D200
; no shadows for AUDC/AUDF
AUDF1  = $D200  ;53760 (W) ; Audio frequency 1
AUDC1  = $D201  ;53761 (W) ; Audio control 1 (distortion/volume)
AUDF2  = $D202  ;53762 (W)
AUDC2  = $D203  ;53763 (W)
AUDF3  = $D204  ;53764 (W)
AUDC3  = $D205  ;53765 (W)
AUDF4  = $D206  ;53766 (W)
AUDC4  = $D207  ;53767 (W)

; POT0-7 shadows at PADDL0-7
POT0   = $D200  ;53760 (R) ; Paddle positions
POT1   = $D201  ;53761 (R)
POT2   = $D202  ;53762 (R)
POT3   = $D203  ;53763 (R)
POT4   = $D204  ;53764 (R) ; pots 3-7 don't exist on XL/XE
POT5   = $D205  ;53765 (R)
POT6   = $D206  ;53766 (R)
POT7   = $D207  ;53767 (R)

AUDCTL = $D208  ;53768 (W) ; Audio control (no shadow)
ALLPOT = $D208  ;53768 (R) (no shadow)
STIMER = $D209  ;53769 (W) (no shadow)
KBCODE = $D209  ;53769 (R) (shadow @ CH)
SKREST = $D20A  ;53770 (W) (latch)
RANDOM = $D20A  ;53770 (R) (no shadow)
POTGO  = $D20B  ;53771 (W) (latch)
; $D20C (53772) is unused
SEROUT = $D20D  ;53773 (W) (no shadow)
SERIN  = $D20D  ;53773 (R) (no shadow)
IRQEN  = $D20E  ;53774 (W) (shadow @ POKMSK)
IRQST  = $D20E  ;53774 (R)
SKCTL  = $D20F  ;53775 (W) (shadow @ SSKCTL)
SKSTAT = $D20F  ;53775 (R)

; $D210 - $D2FF are mirrors of POKEY address space. The "stereo POKEY"
; modification adds a second POKEY chip, usually addressed at $D210.

; PIA
; No shadow regs for PIA regs
PIA    = $D300
PORTA  = $D300  ;54016
PORTB  = $D301  ;54017
PACTL  = $D302  ;54018
PBCTL  = $D303  ;54019

; $D304 - $D3FF are mirrors of PIA address space

; ANTIC
ANTIC  = $D400
DMACTL = $D400  ;54272 (W) (shadow @ SDMCTL)
CHACTL = $D401  ;54273 (W) (shadow @ CHACT)
DLISTL = $D402  ;54274 (W) (shadow @ SDLSTL)
DLISTH = $D403  ;54275 (W) (shadow @ SDLSTH)
HSCROL = $D404  ;54276 (W) (no shadow)
VSCROL = $D405  ;54277 (W) (no shadow)
; $D406 (54278) is unused
PMBASE = $D407  ;54279 (W) (no shadow)
; $D408 (54280) is unused
CHBASE = $D409  ;54281 (W) (shadow @ CHBAS)
WSYNC  = $D40A  ;54282 (W), latch (data written doesn't matter)
VCOUNT = $D40B  ;54283 (R) (no shadow)
PENH   = $D40C  ;54284 (R) (shadow @ LPENH)
PENV   = $D40D  ;54285 (R) (shadow @ LPENV)
NMIEN  = $D40E  ;54286 (W) (no shadow)
NMIRES = $D40F  ;54287 (W), latch?
NMIST  = $D40F  ;54287 (R) (no shadow)

; $D410 - $D4FF are mirrors of ANTIC address space

CCNTL  = $D500  ;54528 Cartridge control (sometimes used for bankswitching)
; $D500 - $D5FF is supposed to be all be mapped to CCNTL

; $D600 - $D7FF is unmapped? used by PBI on XL? seems to read all $FF

;
; FLOATING POINT MATH ROUTINES
;
; From Mapping:
; These entry points are the same on 400/800 and XL OS, though the
; routines themselves are different (bugfixed/optimized for XL)
; Also, on the XL, the $D800 area is bankswitched to PBI device ROM,
; during PBI I/O. Not sure if all of $D800 - $DFFF is switched out
; or just part of it.
AFP    = $D800  ;55296 ASCII to Floating Point (FP) conversion.
FASC   = $D8E6  ;55526 FP value to ASCII conversion.
IFP    = $D9AA  ;55722 Integer to FP conversion
FPI    = $D9D2  ;55762 FP to Integer conversion
ZFR0   = $DA44  ;55876 Clear FR0 (set all bytes to 0)
ZF1    = $DA46  ;55878 Clear FR1 (set all bytes to 0) (aka AF1 (De Re))
FSUB   = $DA60  ;55904 FP subtract: FR0 = FR0 - FR1
FADD   = $DA66  ;55910 FP add:      FR0 = FR0 + FR1
FMUL   = $DADB  ;56027 FP multiply: FR0 = FR0 * FR1
FDIV   = $DB28  ;56104 FP divide:   FR0 = FR0 / FR1
PLYEVL = $DD40  ;56640 FP polynomial evaluation
FLD0R  = $DD89  ;56713 Load FP number into FR0 from 6502 X/Y registers
FLD0P  = $DD8D  ;56717 Load FP number into FR0 from FLPTR
FLD1R  = $DD98  ;56728 Load FP number into FR1 from 6502 X/Y registers
FLD1P  = $DD9C  ;56732 Load FP number into FR1 from FLPTR
FST0R  = $DDA7  ;56743 Store FP number into 6502 X/Y regs from FR0
FST0P  = $DDAB  ;56747 Store FP number from FR0, using FLPTR
FMOVE  = $DDB6  ;56758 Move FP number from FR0 into FR1 (FR1 = FR0)
EXP    = $DDC0  ;56768 FP base e exponentiation
EXP10  = $DDCC  ;56780 FP base 10 exponentiation
LOG    = $DECD  ;57037 FP natural logarithm
LOG10  = $DED1  ;57041 FP base 10 logarithm

;
;
;       OPERATING SYSTEM
;
;
;     MODULE ORIGIN TABLE
;
CHORG  = $E000  ;57344 CHARACTER SET, 1K
VECTBL = $E400  ;58368 VECTOR TABLE
VCTABL = $E480  ;58496 RAM VECTOR INITIAL VALUE TABLE
CIOORG = $E4A6  ;58534 CIO HANDLER
INTORG = $E6D5  ;59093 INTERRUPT HANDLER
SIOORG = $E944  ;59716 SIO DRIVER
DSKORT = $EDEA  ;60906 DISK HANDLER
PRNORG = $EE78  ;61048 PRINTER HANDLER
CASORG = $EE78  ;61048 CASSETTE HANDLER
MONORG = $F0E3  ;61667 MONITOR/POWER UP MODULE
KBDORG = $F3E4  ;62436 KEYBOARD/DISPLAY HANDLER
;
;
;  VECTOR TABLE, CONTAINS ADDRESSES OF CIO ROUTINES IN THE
;  FOLLOWING ORDER. THE ADDRESSES IN THE TABLE ARE TRUE ADDRESSES-1
;
;  ADDRESS + 0  OPEN
;          + 2  CLOSE
;          + 4  GET
;          + 6  PUT
;          + 8  STATUS
;          + A  SPECIAL
;          + C  JMP TO INITIALIZATION
;          + F  NOT USED
;
;

; 20070529 bkw: why are they address minus one? because they are called
; via RTS: a JSR actually pushes the return address minus one, and RTS
; increments the address on the stack after popping it. The Atari OS
; "pretends" to have done a JSR by pushing the address-1 on the stack,
; then executes RTS, which "returns" to the correct address.

EDITRV = $E400  ;58368 EDITOR
SCRENV = $E410  ;58384 SCREEN
KEYBDV = $E420  ;58400 KEYBOARD
PRINTV = $E430  ;58416 PRINTER
CASETV = $E440  ;58432 CASSETTE
;
;        ROM VECTORS
;
; 20070529 bkw: These consist of a JMP xxxx instruction in the ROM.
DSKINV = $E453  ;58451
CIOV   = $E456  ;58454 ; Main CIO entry point!
SIOV   = $E459  ;58457 ; Main SIO entry point!
SETVBV = $E45C  ;58460
SYSVBV = $E45F  ;58463
VBIVAL = $E460  ;58464 ADR AT VVBLKI (operand of JMP @ $E45F)
XITVBV = $E462  ;58466 EXIT VBI
VBIXVL = $E463  ;58467 ADR AT VVBLKD (operand of JMP @ $E462)
SIOINV = $E465  ;58469
SENDEV = $E468  ;58472
INTINV = $E46B  ;58475
CIOINV = $E46E  ;58478
BLKBDV = $E471  ;58481 MEMO PAD MODE (self-test in XL)
WARMSV = $E474  ;58484 ; warmstart (RESET key jumps here)
COLDSV = $E477  ;58487 ; coldstart (reboot) the Atari
RBLOKV = $E47A  ;58490
CSOPIV = $E47D  ;58493

; SYSEQU.ASM defines this:
CIO = CIOV

; XL-only entry points:
XL_SELFSV = BLKBDV ; self-test (same entry point as 800 memo pad)
XL_SELFTST = BLKBDV ; alt. name (Mapping)
XL_PUPDIV = $E480 ;58496 (XL) Power-up ATARI logo (1200XL only), or self-test
XL_SLFTSV = $E483 ;58499 (XL) Self-test vector (points to $5000)
XL_PENTV  = $E486 ;58502 (XL) Entry to the handler uploaded from peripheral
                  ;           or disk (is this for the PBI?)
XL_PHUNLV = $E489 ;58505 (XL) Entry to uploaded handler unlink (PBI?)
XL_PHINIV = $E48C ;58508 (XL) Entry to uploaded handler init (PBI?)
XL_GPDVV  = $E48F ;58511 (XL) General-purpose parallel device handler
                  ;           (copy to HATABS to use)

;;;;; Here endeth the list of official mnemonics

; Mapping has this to say about the XL ROMs:
;Byte              Use
;65518/FFEE        Revision date D1 and D2 (four-bit BCD)
;65519/FFEF        Revision date M1 and M2
;65520/FFF0        Revision date Y1 and Y2
;65521/FFF1        Option byte; should read 1 for the
;                  1200XL (Mapping author's 800XL reads 2)
;65522-26/FFF2-6   Part number in the form AANNNNNN
;65527/FFF7        Revision number (again, mine reads 2)
;65528-9/FFF8-9    Checksum, bytes (LSB/MSB)
; There don't seem to be any known mnemonics for the above...

; 20061120 bkw: display list stuff. These are not official Atari mnemonics,
; but they *are* somewhat based on the "Checkers Demo" by Carol Shaw,
; in the Atari Hardware Manual (she didn't define all these, and she didn't
; use the "DL_" prefix, probably because her assembler was limited to
; 6-character labels and/or didn't support the underscore).

; blank lines, 1-8 scanlines high
DL_BLANK1 = $00
DL_BLANK2 = $10
DL_BLANK3 = $20
DL_BLANK4 = $30
DL_BLANK5 = $40
DL_BLANK6 = $50
DL_BLANK7 = $60
DL_BLANK8 = $70

; modifier bits..
DL_VSCROLL = $10
DL_HSCROLL = $20
DL_LMS = $40
DL_DLI = $80

; graphics modes (these are the BASIC modes)
; If you're more familiar with the ANTIC modes, nobody's forcing you
; to use these :)
DL_GR0 = $02
DL_GR1 = $06
DL_GR2 = $07
DL_GR3 = $08
DL_GR4 = $09
DL_GR5 = $0A
DL_GR6 = $0B
DL_GR7 = $0D
DL_GR8 = $0F
DL_GR12 = $04 ; GR. 12-15 only supported by GRAPHICS command on XL/XE,
DL_GR13 = $05 ; but they exist on all ANTIC revisions
DL_GR14 = $0C
DL_GR15 = $0E ; AKA "graphics 7.5"
; No GRAPHICS mode for ANTIC $03 (true descender) mode

; jump instructions
DL_JMP = $01 ; jump without vertical blank (used to skip over 1K boundary)
DL_JVB = $41 ; jump & wait for VBLANK (end of display list)

; How to use the above: here's a sample display list for GR.0, with a DLI
; on screen line 10.

; dlist:
;  ; 4*8 = 32 blank lines at start of display
;  byte DL_BLANK8, DL_BLANK8, DL_BLANK8, DL_BLANK8
;
;  byte DL_GR0 | DL_LMS   ; display GR.0 line, and load screen memory address..
;  word screen_ram        ; ...from our screen memory (declared elsewhere)
;
;  ; 8 more GR.0 lines
;  byte DL_GR0, DL_GR0, DL_GR0, DL_GR0, DL_GR0, DL_GR0, DL_GR0, DL_GR0
;
;  byte DL_GR0 | DL_DLI ; another GR.0 line, with the DLI bit enabled
;
;  ; lines 11-24 (14 more GR.0 bytes)
;  byte DL_GR0, DL_GR0, DL_GR0, DL_GR0, DL_GR0, DL_GR0, DL_GR0, DL_GR0
;  byte DL_GR0, DL_GR0, DL_GR0, DL_GR0, DL_GR0, DL_GR0
;
;  ; that's 24 lines, so finish with a VBLANK
;  byte DL_JVB ; jump (and wait), to...
;  word dlist  ; ...the beginning.
