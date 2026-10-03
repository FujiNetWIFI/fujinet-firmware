local n = 0
_G._p = emu.add_machine_frame_notifier(function()
    n = n + 1
    if n ~= 10 then return end
    for pname, port in pairs(manager.machine.ioport.ports) do
        print("PORT " .. pname)
        for fname, _ in pairs(port.fields) do print("   field: " .. fname) end
    end
    manager.machine:exit()
end)
