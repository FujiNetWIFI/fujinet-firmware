; fujiboot.asm -- M2: mount a cartridge image over the network and boot it.
;
; MOUNT_HOST -> SET_DEVICE_FULLPATH -> MOUNT_IMAGE. The last one makes the
; ESP32 side stream the file back to the cartridge on the same link,
; addressed to the DBC device, while MOUNT_IMAGE's own reply is outstanding;
; the cart stages it and reports BOOT_STATE (and draws the progress strip).
; Then BOOTLOCK, and fn_handover starts the BIOS over: the cart swaps the
; image in on the BIOS's first fetch.
;
; Target host slot and path come from bootcfg.inc, written by build.sh:
;   BOOT_HOST=0 BOOT_PATH=/game.st2 ./build.sh fujiboot

        CPU     1802
        INCLUDE "fujinet.inc"
        INCLUDE "s2macro.inc"
        INCLUDE "bootcfg.inc"

DEVSLOT EQU     0

        ORG     0400H
        INCLUDE "s2call.inc"
        INCLUDE "fujilib.inc"
        INCLUDE "fujidisp.inc"

        ORG     0C00H

PUTS    MACRO   rc, str
        LDI     rc
        FCALL   d_at
        LDR     R8, str
        FCALL   d_puts
        ENDM

; STEP n: RA.0 = n, then fn_go and fn_ack; on failure to `show` with RA.1.
STEP    MACRO   n
        LDI     n
        PLO     RA
        FCALL   go_ack
        BZ      $+5                     ; over the FJMP
        FJMP    show
        ENDM

        FIT     250
main:   FCALL   d_cls
        PUTS    00H, t_title
        PUTS    20H, t_path
        LDI     30H
        FCALL   d_at
        LDR     R8, bootpath
        FCALL   d_puts
        PUTS    50H, t_mount
        LDI     0
        PLO     RA
        LDI     FNENOC
        PHI     RA
        FCALL   fn_chk
        BDF     $+5
        FJMP    show

        ; ---- MOUNT_HOST(host) ----
        LDI     FNDEVF
        PHI     R9
        LDI     FNCMHST
        PLO     R9
        LDI     1
        FCALL   fn_beg
        LDI     BOOTHST
        FCALL   fn_pb
        STEP    1

        ; ---- SET_DEVICE_FULLPATH(dev, host, mode, path) ----
        ; three one-byte parameters, then a FULL 256-byte NUL-padded path
        LDI     FNDEVF
        PHI     R9
        LDI     FNCSDFP
        PLO     R9
        LDI     3
        FCALL   fn_beg
        LDI     DEVSLOT
        FCALL   fn_pb
        LDI     BOOTHST
        FCALL   fn_pb
        LDI     FMREAD
        FCALL   fn_pb
        LDR     R8, bootpath
        FCALL   fn_path
        STEP    2

        ; ---- MOUNT_IMAGE(dev, mode): the ESP32 streams the file now ----
        LDI     FNDEVF
        PHI     R9
        LDI     FNCMIMG
        PLO     R9
        LDI     2
        FCALL   fn_beg
        LDI     DEVSLOT
        FCALL   fn_pb
        LDI     FMREAD
        FCALL   fn_pb
        LDR     R8, V_TMO
        LDI     66                      ; past the cart's 60 s for MOUNT_IMAGE
        STR     R8
        STEP    3
        FJMP    stage

; go_ack -- fn_go, then fn_ack: D = 0, or RA.1 = D = the error.
        FIT     24
go_ack: FCALL   fn_go
        BNZ     gak1
        FCALL   fn_ack
gak1:   PHI     RA
        FRET

; ---- wait for the staged image, then hand over ----
        FIT     80
stage:  LDI     4
        PLO     RA
        LDI     10
        FCALL   fn_frames
stg1:   PEEK    FN_BOOTSTATE
        XRI     FN_BOOT_READY
        BZ      stg3
        XRI     FN_BOOT_READY ! FN_BOOT_FAILED  ; AS: ! is XOR, ^ is power
        BZ      stg2
        FCALL   fn_tick
        BDF     stg1
        LDI     FNEWAIT
        PHI     RA
        BR      show
stg2:   PEEK    FN_BOOTERR
        PHI     RA
        LDI     5
        PLO     RA
        BR      show
stg3:   FCALL   fn_boot                 ; returns only if never armed
        PHI     RA
        LDI     6
        PLO     RA

; ---- the failure screen: WHICH step, with WHAT ----
show:   PUTS    70H, t_fail
        GLO     RA
        FCALL   d_hex
        LDI     'E'
        FCALL   d_putc
        GHI     RA
        FCALL   d_hex
hang:   BR      hang

t_title: DB     "FUJINET BOOT", 0
t_path:  DB     "IMAGE:", 0
t_mount: DB     "MOUNTING...", 0
t_fail:  DB     "FAILED ", 0
bootpath:
        BOOTPATH

        S2CLAIM
