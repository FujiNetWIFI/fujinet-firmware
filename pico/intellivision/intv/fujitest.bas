' fujitest.bas -- end-to-end FujiNet demo.
'
' Sends GET_ADAPTERCONFIG_EXTENDED (device 0x70, command 0xC4) to FujiNet
' through the RP2040 cartridge's mailbox at $9800-$9FFF, and prints the
' SSID, firmware version, and IP address from the reply to the TV screen.
'
' Mailbox layout must match pico/intellivision/firmware/fuji_mailbox.h
' exactly -- IntyBASIC has no #include, so these CONSTs are the
' hand-synchronized copy documented there.

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
    CONST FN_RX=$9940

    CONST FUJI_DEVICEID_FUJINET=$70
    CONST FUJICMD_GET_ADAPTERCONFIG_EXTENDED=$C4
    CONST FUJICMD_ACK=$06

    ' Offsets into the 240-byte AdapterConfigExtended reply, from
    ' lib/device/fujiDevice.h in the main fujinet-firmware tree.
    CONST OFF_SSID=0
    CONST OFF_FNVER=125
    CONST OFF_SLOCALIP=140

    CONST ROW=20 ' cells per screen row (20x12 display)

    cls
    print at 0 color 7,"FUJINET INTV TEST"

    ' Bounded wait for the RP2040 mailbox to come up (magic bytes 'F','N').
    ' 180 frames = 3s at NTSC.
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
    if seq=0 then seq=1 ' skip 0: ACKSEQ starts at 0, so SEQ=0 would look pre-acked
    poke (FN_SEQ),seq

    ' Bounded wait for the reply. 600 frames = 10s at NTSC, comfortably
    ' longer than the RP2040 side's own 5s per-transaction budget.
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

    print at ROW*3,"SSID:"
    gosub print_ssid

    print at ROW*4,"VERSION:"
    gosub print_ver

    print at ROW*5,"IP:"
    gosub print_ip

halt:
    wait
    goto halt

print_ssid: procedure
    for i=0 to 20
        c=peek(FN_RX+OFF_SSID+i) and 255
        if c=0 then return
        if c<32 or c>126 then c=32
        print at ROW*3+6+i,(c-32)*8+7
    next i
end

print_ver: procedure
    for i=0 to 14
        c=peek(FN_RX+OFF_FNVER+i) and 255
        if c=0 then return
        if c<32 or c>126 then c=32
        print at ROW*4+9+i,(c-32)*8+7
    next i
end

print_ip: procedure
    for i=0 to 15
        c=peek(FN_RX+OFF_SLOCALIP+i) and 255
        if c=0 then return
        if c<32 or c>126 then c=32
        print at ROW*5+4+i,(c-32)*8+7
    next i
end
