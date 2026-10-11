; fujilib.inc -- the 1802 side of the FujiNet cartridge mailbox: the code.
;
; The Studio II edge has no write strobe, so every byte reaches the cart in
; the address of a read: a register write is a read of FN_REGSEL+n, then of
; FN_REGDATA+v. Every routine here is FCALLed, may clobber R8 and R9, and
; returns its result in D.

; fn_rw -- register R8.0 = D. Leaves R9 alone (fn_beg needs it).
fn_rw:  PHI     RD                      ; RD.1 is free between calls
        LDI     HI(FN_REGSEL)
        PHI     RF
        GLO     R8
        PLO     RF
        LDN     RF
        LDI     HI(FN_REGDATA)
        PHI     RF
        GHI     RD
        PLO     RF
        LDN     RF
        LDI     FN_PARK
        PHI     RF
        FRET

; fn_txb -- append D to the TX stream.
fn_txb: HOT     HI(FN_TXDATA)
        FRET

; fn_pb -- append a one-byte parameter with value D.
fn_pb:  PLO     R9
        LDI     1
        HOT     HI(FN_TXDATA)
        GLO     R9
        HOT     HI(FN_TXDATA)
        FRET

; fn_pw -- append a two-byte little-endian parameter, R8.
fn_pw:  LDI     2
        HOT     HI(FN_TXDATA)
        GLO     R8
        HOT     HI(FN_TXDATA)
        GHI     R8
        HOT     HI(FN_TXDATA)
        FRET

; fn_path -- append the NUL-terminated string at R8 to the TX stream, padded
; with NULs to EXACTLY 256 bytes: OPEN_DIRECTORY and SET_DEVICE_FULLPATH
; both read exactly 256. R9.0 counts.
        FIT     32
fn_path:
        LDI     0
        PLO     R9
        LDI     HI(FN_TXDATA)
        PHI     RF
fpth1:  LDA     R8
        BZ      fpth2
        PLO     RF
        LDN     RF
        DEC     R9
        GLO     R9
        BNZ     fpth1
        BR      fpth4
fpth2:  DEC     R8                      ; hold on the NUL
fpth3:  LDI     0
        PLO     RF
        LDN     RF
        DEC     R9
        GLO     R9
        BNZ     fpth3
fpth4:  LDI     FN_PARK
        PHI     RF
        FRET

; fn_chk -- DF = 1 if a FujiNet cartridge is answering ('F','N').
        FIT     40
fn_chk: PEEK    FN_MAGIC
        XRI     'F'
        BNZ     fchk0
        PEEK    FN_MAGIC+1
        XRI     'N'
        BNZ     fchk0
        SMI     0                       ; D = 0: DF = 1
        FRET
fchk0:  ADI     0                       ; D != 0, no carry: DF = 0
        FRET

; fn_beg -- begin a transaction: R9.1 = device, R9.0 = command, D = number
; of parameters. Rewinds the TX stream last, so parameters may be appended
; at once. Sets the timeout to 12 s; a slow command raises V_TMO after this.
fn_beg: STXD
        LDI     FN_REG_DEVICE
        PLO     R8
        GHI     R9
        FCALL   fn_rw
        LDI     FN_REG_CMD
        PLO     R8
        GLO     R9
        FCALL   fn_rw
        IRX
        LDX
        PHI     R8
        LDI     FN_REG_NPARAM
        PLO     R8
        GHI     R8
        FCALL   fn_rw
        LDI     FN_REG_DATA_RST
        PLO     R8
        LDI     0
        FCALL   fn_rw
        LDR     R8, V_TMO
        LDI     12
        STR     R8
        FRET

; fn_frames -- start a countdown of D seconds (x 64 frames, near enough).
fn_frames:
        PLO     R9
        LDR     R8, V_CNT
        GLO     R9
        SHR
        SHR
        STR     R8                      ; hi = secs / 4
        INC     R8
        GLO     R9
        ANI     3
        SHL
        SHL
        SHL
        SHL
        SHL
        SHL
        STR     R8                      ; lo = (secs % 4) * 64
        INC     R8
        GLO     RE
        STR     R8                      ; V_LAST
        FRET

; fn_tick -- count the countdown down by one if a frame has passed since the
; last call: poll it at least once a frame. DF = 1 while time remains, 0 once
; it has run out.
        FIT     50
fn_tick:
        LDR     R9, V_CNT
        LDA     R9
        BNZ     ftk1
        LDN     R9
        BNZ     ftk1
        ADI     0                       ; D = 0: DF = 0, expired
        FRET
ftk1:   LDR     R8, V_LAST
        GLO     RE
        SEX     R8
        XOR
        SEX     R2
        BZ      ftk2                    ; the same frame
        GLO     RE
        STR     R8
        LDR     R9, V_CNT               ; hi, lo
        LDA     R9
        PHI     R8
        LDN     R9
        PLO     R8
        DEC     R8
        GLO     R8
        STR     R9
        DEC     R9
        GHI     R8
        STR     R9
ftk2:   SMI     0                       ; DF = 1
        FRET

; fn_go -- commit and wait. D = FNE* (0: the transaction completed). The
; next sequence number comes from the CART's ACKSEQ + 1, never a counter in
; RAM: a power cycle restarts the client, not the cart.
        FIT     88
fn_go:  PEEK    FN_ACKSEQ
        ADI     1
        BNZ     fgo1
        LDI     1                       ; 0 is reserved as "never used"
fgo1:   PHI     R9
        LDR     R8, V_SEQ
        GHI     R9
        STR     R8
        LDI     FN_REG_SEQ
        PLO     R8
        GHI     R9
        FCALL   fn_rw                   ; launching it is this one pair
        LDR     R8, V_TMO
        LDN     R8
        FCALL   fn_frames
fgo2:   PEEK    FN_ACKSEQ               ; published LAST, after the reply
        STR     R2
        LDR     R8, V_SEQ
        LDN     R8
        XOR
        BZ      fgo3
        FCALL   fn_tick
        BDF     fgo2
        LDI     FNEWAIT
        FRET
fgo3:   PEEK    FN_ERR
        FRET

; fn_ack -- 0 on success, FNENAK if the server said no. After fn_go gave 0.
        FIT     20
fn_ack: PEEK    FN_REPLYCMD
        XRI     06H
        BZ      fack1
        LDI     FNENAK
fack1:  FRET

; fn_boot -- arm the staged image and hand the console to it. Returns only if
; the cart never armed the swap, with D = FNENARM.
fn_boot:
        LDI     FN_REG_BOOTLOCK
        PLO     R8
        LDI     FN_BOOTLOCK_MAGIC
        FCALL   fn_rw
        FJMP    fn_arm

; fn_config -- back to CONFIG. Returns only if the cart never armed it.
fn_config:
        LDI     FN_REG_CONFIG
        PLO     R8
        LDI     FN_CONFIG_MAGIC
        FCALL   fn_rw
        FJMP    fn_arm                  ; never fall into a FIT's gap

; fn_arm -- wait for FN_ARMED, then hand over; D = FNENARM if it never is.
        FIT     32
fn_arm: LDI     2
        FCALL   fn_frames
farm1:  PEEK    FN_ARMED
        BNZ     farm2
        FCALL   fn_tick
        BDF     farm1
        LDI     FNENARM
        FRET
farm2:  FJMP    fn_handover
