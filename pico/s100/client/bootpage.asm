;=====================================================================
; BOOTPAGE.ASM -- the card's 256-byte boot page, 8080 only.
;
; The RP serves this page at the DIP-switched address (FF00h by default)
; and patches in its real page and pipe base: tools/mkpage.py assembles it
; twice to find those bytes. RAM size is unknown here, so nothing below
; uses the stack.
;
;   page+0  load CONFIG (image 0) from the card and run it
;   page+3  load the boot loader (image 1) and run it; CONFIG's Boot
;
; A LOAD streams {u16 len, u16 load, u16 entry, base, page, console,
; flags} and then the image through GETC.
;=====================================================================

PAGE    EQU     0FF00h          ; patched: the page's high byte
PIPE    EQU     0A0h            ; patched: the pipe base
GETC    EQU     PIPE
STATUS  EQU     PIPE+1
CONTROL EQU     PIPE+3
LOAD    EQU     20h
NOIMAGE EQU     80h

        ORG     PAGE

        JP      CONFIG
        JP      LOADER

CONFIG: LD      A,LOAD+0
        JP      START
LOADER: LD      A,LOAD+1
START:  DI
        OUT     (CONTROL),A
H1:     IN      A,(STATUS)      ; length -> BC
        RLA
        JP      NC,H1
        IN      A,(GETC)
        LD      C,A
H2:     IN      A,(STATUS)
        RLA
        JP      NC,H2
        IN      A,(GETC)
        LD      B,A
H3:     IN      A,(STATUS)      ; load address -> HL
        RLA
        JP      NC,H3
        IN      A,(GETC)
        LD      L,A
H4:     IN      A,(STATUS)
        RLA
        JP      NC,H4
        IN      A,(GETC)
        LD      H,A
H5:     IN      A,(STATUS)      ; entry -> DE
        RLA
        JP      NC,H5
        IN      A,(GETC)
        LD      E,A
H6:     IN      A,(STATUS)
        RLA
        JP      NC,H6
        IN      A,(GETC)
        LD      D,A
H7:     IN      A,(STATUS)      ; base, page, console: for the image, not us
        RLA
        JP      NC,H7
        IN      A,(GETC)
H8:     IN      A,(STATUS)
        RLA
        JP      NC,H8
        IN      A,(GETC)
H9:     IN      A,(STATUS)
        RLA
        JP      NC,H9
        IN      A,(GETC)
H10:    IN      A,(STATUS)      ; flags
        RLA
        JP      NC,H10
        IN      A,(GETC)
        AND     NOIMAGE
        JP      NZ,NONE
COPY:   LD      A,B
        OR      C
        JP      Z,RUN
C1:     IN      A,(STATUS)
        RLA
        JP      NC,C1
        IN      A,(GETC)
        LD      (HL),A
        INC     HL
        DEC     BC
        JP      COPY
RUN:    EX      DE,HL
        JP      (HL)
NONE:   HALT                    ; no such image on the card
        JP      NONE
