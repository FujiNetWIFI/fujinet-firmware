local AT = tonumber(os.getenv("PROBE_AT") or "1500")
local n = 0
_G._p = emu.add_machine_frame_notifier(function()
    n = n + 1
    if n ~= AT then return end
    local sp = manager.machine.devices[":maincpu"].spaces["program"]
    print(string.format("CFG: PC0=%04X VSTATE=%02X VCUR=%02X VNROW=%02X VHOST=%02X",
        manager.machine.devices[":maincpu"].state["PC0"].value,
        sp:readv_u8(0x8006), sp:readv_u8(0x8007), sp:readv_u8(0x8008), sp:readv_u8(0x800F)))
    print(string.format("CFG: reply[0..3] = %02X %02X %02X %02X  ERR=%02X RCMD=%02X",
        sp:readv_u8(0xF800), sp:readv_u8(0xF801), sp:readv_u8(0xF802), sp:readv_u8(0xF803),
        sp:readv_u8(0xFC02), sp:readv_u8(0xFC03)))
    manager.machine:exit()
end)
