;=====================================================================
; BOOTCHECK.ASM -- a stand-in for CONFIG as image 0: says hello on the
; 2SIO console, then boots drive 0 through the page's loader entry,
; exactly as CONFIG's Boot command does. 8080 only.
;=====================================================================

        ORG     0100h

        JP      START
        DEFB    "FNS1"
IBASE:  DEFB    0
IPAGE:  DEFB    0
ICON:   DEFB    0
IFLAGS: DEFB    0
ISPT:   DEFB    0

START:  LD      SP,0100h
        LD      A,03h           ; 6850 master reset, then 8N1 /16
        OUT     (10h),A
        LD      A,15h
        OUT     (10h),A
        LD      HL,MSG
PR1:    LD      A,(HL)
        OR      A
        JP      Z,BOOT
PR2:    IN      A,(10h)
        AND     02h
        JP      Z,PR2
        LD      A,(HL)
        OUT     (11h),A
        INC     HL
        JP      PR1
BOOT:   LD      A,(IPAGE)       ; JP page+3: load and run the boot loader
        LD      H,A
        LD      L,3
        JP      (HL)

MSG:    DEFB    13,10,"BOOTCHECK: image 0 ran, booting drive 0",13,10,0
