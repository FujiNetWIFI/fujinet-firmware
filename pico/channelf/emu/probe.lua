local n = 0
_G._probe = emu.add_machine_frame_notifier(function()
    n = n + 1
    if n ~= 300 then return end
    local cpu = manager.machine.devices[":maincpu"]
    local sp = cpu.spaces["program"]
    print(string.format("PROBE: J=%02X", cpu.state["J"].value))
    print(string.format("PROBE: VHEX $8000..2 = %02X %02X %02X ('%s%s')",
        sp:readv_u8(0x8000), sp:readv_u8(0x8001), sp:readv_u8(0x8002),
        string.char(sp:readv_u8(0x8000)), string.char(sp:readv_u8(0x8001))))
    print(string.format("PROBE: ACKSEQ $FC00=%02X ERR $FC02=%02X RXLEN=%02X%02X",
        sp:readv_u8(0xFC00), sp:readv_u8(0xFC02),
        sp:readv_u8(0xFC05), sp:readv_u8(0xFC04)))
    manager.machine:exit()
end)
