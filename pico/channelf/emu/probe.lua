local AT = tonumber(os.getenv("PROBE_AT") or "1500")
local n = 0
_G._probe = emu.add_machine_frame_notifier(function()
    n = n + 1
    if n ~= AT then return end
    local cpu = manager.machine.devices[":maincpu"]
    local sp = cpu.spaces["program"]
    local s = cpu.state
    print(string.format("PROBE: PC0=%04X PC1=%04X DC0=%04X A=%02X J=%02X",
        s["PC0"].value, s["PC1"].value, s["DC0"].value, s["A"].value, s["J"].value))
    print(string.format("PROBE: ACKSEQ=%02X ERR=%02X RCMD=%02X RXLEN=%02X%02X BSTAT=%02X",
        sp:readv_u8(0xFC00), sp:readv_u8(0xFC02), sp:readv_u8(0xFC03),
        sp:readv_u8(0xFC05), sp:readv_u8(0xFC04), sp:readv_u8(0xFC06)))
    local p = ""
    for i = 0, 15 do
        local c = sp:readv_u8(0x8100 + i)
        p = p .. (c >= 32 and c < 127 and string.char(c) or string.format("<%02X>", c))
    end
    print("PROBE: VPATH = " .. p)
    print(string.format("PROBE: VCUR=%02X VNROW=%02X VPOS=%02X%02X VMORE=%02X",
        sp:readv_u8(0x8006), sp:readv_u8(0x8007),
        sp:readv_u8(0x8009), sp:readv_u8(0x8008), sp:readv_u8(0x800A)))
    manager.machine:exit()
end)
