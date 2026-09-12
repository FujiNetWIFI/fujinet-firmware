local n = 0
_G._ports = emu.add_machine_frame_notifier(function()
    n = n + 1
    if n ~= 5 then return end
    for tag, port in pairs(manager.machine.ioport.ports) do
        for fname, f in pairs(port.fields) do
            print(string.format("%-28s %s", tag, fname))
        end
    end
    manager.machine:exit()
end)
