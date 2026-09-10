local sp, n = nil, 0
_G._np = emu.add_machine_frame_notifier(function()
    n = n + 1
    sp = sp or manager.machine.devices[":maincpu"].spaces["program"]
    if n % 1200 ~= 0 then return end
    print(string.format("N: t=%d AV=%02X%02X PRV=%02X%02X MIN=%02X%02X TRY=%02X ST=%02X",
        n, sp:readv_u8(0x8014), sp:readv_u8(0x8013),
        sp:readv_u8(0x8016), sp:readv_u8(0x8015),
        sp:readv_u8(0x8027), sp:readv_u8(0x8026),
        sp:readv_u8(0x8017), sp:readv_u8(0x8006)))
    if n > 12000 then manager.machine:exit() end
end)
