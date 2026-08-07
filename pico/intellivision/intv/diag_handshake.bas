' Diagnostic: the real mailbox handshake from fujitest.bas (both WHILE loops,
' the POKEs, the SEQ bump) but with the print_ssid/print_ver/print_ip
' GOSUB procedures removed -- isolates whether the handshake loop itself is
' the problem, vs. the byte-by-byte string-extraction procedures that run
' after a successful handshake.
    ASM MEMATTR $8000, $9FFF, "+RWN"

    CONST FN_MAGIC0=$9800
    CONST FN_MAGIC1=$9801
    CONST FN_SEQ=$9803
    CONST FN_ACKSEQ=$9804
    CONST FN_DEVICE=$9805
    CONST FN_CMD=$9806
    CONST FN_NPARAM=$9807
    CONST FN_TXLEN_LO=$9808
    CONST FN_TXLEN_HI=$9809
    CONST FN_REPLY_CMD=$980E

    CONST FUJI_DEVICEID_FUJINET=$70
    CONST FUJICMD_GET_ADAPTERCONFIG_EXTENDED=$C4
    CONST FUJICMD_ACK=$06

    CONST ROW=20

    cls
    print at 0 color 7,"FUJINET INTV TEST"

    #t=0
    while (peek(FN_MAGIC0)<>70) and (peek(FN_MAGIC1)<>78) and (#t<180)
        #t=#t+1
        wait
    wend
    if #t>=180 then
        print at ROW*2,"NO CARTRIDGE MAILBOX"
        goto halt
    end if

    print at ROW*2,"MAILBOX UP - SENDING..."

    poke (FN_DEVICE),FUJI_DEVICEID_FUJINET
    poke (FN_CMD),FUJICMD_GET_ADAPTERCONFIG_EXTENDED
    poke (FN_NPARAM),0
    poke (FN_TXLEN_LO),0
    poke (FN_TXLEN_HI),0

    seq=seq+1
    if seq=0 then seq=1
    poke (FN_SEQ),seq

    #t=0
    while (peek(FN_ACKSEQ)<>seq) and (#t<600)
        #t=#t+1
        wait
    wend
    if #t>=600 then
        print at ROW*3,"TIMEOUT - NO FUJINET"
        goto halt
    end if

    if peek(FN_REPLY_CMD)<>FUJICMD_ACK then
        print at ROW*3,"FUJINET RETURNED ERROR"
        goto halt
    end if

    print at ROW*3,"GOT ACK REPLY"

halt:
    wait
    goto halt
