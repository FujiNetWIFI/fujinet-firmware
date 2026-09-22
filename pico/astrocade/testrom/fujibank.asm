; fujibank.asm -- APPBANK self-test client for the FujiNet cartridge.
;
; Built as an ordinary claimed 8K client, then tools/mkbanked.py appends
; stamped 4K pages (default 8 total). All code lives in the HIGH half
; (3000H+): the low half is what banks, so nothing here may execute from it.
; The reset entry at exactly 3000H re-selects page 0 and restarts -- every
; appended page's stamped sentinel header points its start vector there,
; which is what makes console RESET safe with any page selected.
;
; For each page: select it with one FNBKSEL read and verify the packer's
; stamp (page 0 = our own low half; page 1 = the high half aliased low;
; pages 2+ = header, marker pair, 32 PRNG bytes). Then prove the mailbox is
; fully live WHILE a non-zero page is banked: run GET_ADAPTERCONFIG_EXTENDED
; with page 2 selected. Verdict to screen and to 4FA0H (0A5H pass / 5AH
; fail, failing page in 4FA1H) for the emulation harness to sample.
;
; Assemble: zmac fujibank.asm; then mkbanked.py (build.sh does both).

        INCLUDE "HVGLIB.H"

LINBUF  EQU     4F00H           ; 40-byte display string buffer
HEXBUF  EQU     4F30H           ; "XX",0
NPAGES  EQU     4F40H           ; total page count, read from the image
CURPG   EQU     4F41H           ; page under test
STACK   EQU     4FC0H
VERDICT EQU     4FA0H           ; 0A5H pass / 5AH fail
VERPAGE EQU     4FA1H           ; failing page

VPASS   EQU     0A5H
VFAILV  EQU     05AH

NPGCELL EQU     2FF0H           ; mkbanked stamps the page count here (page 0)

LINES   EQU     96
OPTFB   EQU     0CH

        ORG     FIRSTC
        DB      55H             ; sentinel: a menued cartridge
        DW      MENUST
        DW      PRGNAM
        DW      RSTENT
PRGNAM: DB      "FUJINET BANK TEST"
        DB      0

; ---- high half: everything that must survive a low-half switch ---------
        ORG     3000H

; The reset entry, at the address every stamped page header points to.
RSTENT: LD      A,(FNBKSEL+0)   ; select page 0
        JP      START

START:  DI                      ; polled operation, no interrupts: I stays 0
        LD      SP,STACK
        SYSTEM  INTPC
        DO      SETOUT
        DB      LINES*2
        DB      0
        DB      8
        DO      COLSET
        DW      PALET
        DO      FILL
        DW      NORMEM
        DW      LINES*BYTEPL
        DB      0
        DO      STRDIS
        DB      2
        DB      2
        DB      OPTFB
        DW      TSTR
        EXIT

        XOR     A
        LD      (VERDICT),A

        CALL    FNCHECK
        JR      Z,HAVMB
        SYSSUK  STRDIS
        DB      2
        DB      14
        DB      OPTFB
        DW      NOCART
        JR      FAIL0

HAVMB:  LD      A,(NPGCELL)     ; page count, stamped by mkbanked
        LD      (NPAGES),A
        OR      A
        JR      Z,FAIL0         ; not a banked image at all
        LD      DE,124+2*256
        CALL    HEXAT

; ---- walk every page ---------------------------------------------------
        XOR     A
PGLOOP: LD      (CURPG),A
        CALL    BANKSEL
        LD      A,(CURPG)
        LD      DE,60+14*256    ; running page number
        CALL    HEXAT
        LD      A,(CURPG)
        CALL    VERPG
        JR      C,FAIL
        LD      A,(CURPG)
        INC     A
        LD      HL,NPAGES
        CP      (HL)
        JR      C,PGLOOP

; ---- mailbox transaction with a non-zero page banked -------------------
        LD      A,2
        CALL    BANKSEL
        LD      A,FNDFUJI
        LD      E,FCACFGX
        LD      L,0
        CALL    FNBEGIN
        LD      B,8             ; ~16 s: covers ESP32 enumeration
        CALL    FNCOMMIT
        JR      C,TFAIL
        OR      A
        JR      NZ,TFAIL
        LD      A,2             ; the transaction must not have moved us
        CALL    VERPG
        JR      C,FAIL
        LD      A,(FNBKSEL+0)   ; back to page 0

; ---- verdict -----------------------------------------------------------
        SYSSUK  STRDIS
        DB      2
        DB      26
        DB      OPTFB
        DW      PASSTR
        LD      A,VPASS
        LD      (VERDICT),A
HALTP:  JR      HALTP

TFAIL:  LD      A,(FNBKSEL+0)
        SYSSUK  STRDIS
        DB      2
        DB      26
        DB      OPTFB
        DW      TXSTR
        JR      FAIL1
FAIL0:  XOR     A
        LD      (CURPG),A
FAIL:   LD      A,(FNBKSEL+0)
FAIL1:  SYSSUK  STRDIS
        DB      2
        DB      36
        DB      OPTFB
        DW      FAILSTR
        LD      A,(CURPG)
        LD      (VERPAGE),A
        LD      DE,60+36*256
        CALL    HEXAT
        LD      A,VFAILV
        LD      (VERDICT),A
HALTF:  JR      HALTF

; ---- helpers -----------------------------------------------------------

; Select page A at 2000H-2FFFH. Clobbers A,D,E.
BANKSEL:
        LD      D,FNRSELP
        ADD     A,80H
        LD      E,A
        LD      A,(DE)
        RET

; One PRNG step, matching mkbanked.py: x = (5x + 47H) & FFH.
; A = x in, A = x' out. Clobbers B.
PRSTEP: LD      B,A
        ADD     A,A
        ADD     A,A
        ADD     A,B
        ADD     A,47H
        RET

; Verify page A as currently mapped at 2000H. Carry set on failure.
; Clobbers A,B,C,D,E,H,L.
VERPG:  LD      C,A
        OR      A
        JR      Z,VP0
        DEC     A
        JR      Z,VP1

; pages 2+: stamped header, marker pair, 32 PRNG bytes from 2020H
        LD      A,(2000H)
        CP      55H
        JR      NZ,VPBAD
        LD      A,(2007H)
        CP      C
        JR      NZ,VPBAD
        LD      A,(2008H)
        LD      B,A
        LD      A,C
        CPL
        CP      B
        JR      NZ,VPBAD
        LD      A,C
        XOR     0A5H            ; PRNG seed
        LD      HL,2020H
        LD      E,32
VPLOOP: CALL    PRSTEP
        CP      (HL)
        JR      NZ,VPBAD
        INC     HL
        DEC     E
        JR      NZ,VPLOOP
        OR      A
        RET

VP0:    LD      A,(2000H)       ; page 0: our own boot half
        CP      55H
        JR      NZ,VPBAD
        LD      A,(NPGCELL)
        LD      HL,NPAGES
        CP      (HL)
        JR      NZ,VPBAD
        OR      A
        RET

VP1:    LD      HL,2000H        ; page 1 IS the high half: byte-compare it
        LD      DE,RSTENT
        LD      B,16
VP1LP:  LD      A,(DE)
        CP      (HL)
        JR      NZ,VPBAD
        INC     HL
        INC     DE
        DJNZ    VP1LP
        OR      A
        RET

VPBAD:  SCF
        RET

        INCLUDE "fujilib.inc"
        INCLUDE "fujidisp.inc"

; ---- data --------------------------------------------------------------
TSTR:   DB      "FN BANK TEST PGS",0
NOCART: DB      "NO FUJINET CART",0
PASSTR: DB      "PASS",0
TXSTR:  DB      "TXN FAIL",0
FAILSTR:DB      "FAIL PAGE",0
PALET:  DB      0E7H,40H,20H,00H
        DB      0E7H,40H,20H,00H
