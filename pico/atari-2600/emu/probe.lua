local n, sp = 0, nil
_G._probe = emu.add_machine_frame_notifier(function()
    n = n + 1
    if n ~= 30 then return end
    sp = manager.machine.devices[":maincpu"].spaces["program"]
    print(string.format("before: FLAGS=%02X  $1C00=%02X  $1E00=%02X  $1B00=%02X",
        sp:readv_u8(0x1F0F), sp:readv_u8(0x1C00), sp:readv_u8(0x1E00), sp:readv_u8(0x1B00)))
    -- Do the arming pair from the emulator side. If the device reacts, the
    -- write handler is installed and the client is at fault; if not, it isn't.
    sp:write_u8(0x1CFC, 0xB5)
    sp:write_u8(0x1CFD, 0x4A)
    print(string.format("after : FLAGS=%02X", sp:readv_u8(0x1F0F)))
    manager.machine:exit()
end)
