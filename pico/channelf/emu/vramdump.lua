local n = 0
_G._vd = emu.add_machine_frame_notifier(function()
    n = n + 1
    if n ~= 200 then return end
    local r = manager.machine.memory.regions[":vram"]
    if not r then print("VD: no :vram region"); manager.machine:exit(); return end
    print("VD: vram size " .. r.size)
    for _, row in ipairs({0, 4, 5, 6, 10, 63}) do
        local s = string.format("VD: row %2d cols0-15:", row)
        for c = 0, 15 do s = s .. string.format(" %d", r:read_u8(row * 128 + c) & 3) end
        s = s .. string.format("   c125=%d c126=%d c127=%d",
            r:read_u8(row * 128 + 125) & 3,
            r:read_u8(row * 128 + 126) & 3,
            r:read_u8(row * 128 + 127) & 3)
        print(s)
    end
    manager.machine:exit()
end)
