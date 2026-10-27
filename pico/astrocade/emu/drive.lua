-- drive.lua: press keypad "1" on the OS menu at emulated t=3 to launch the
-- client, and again at t=25 to launch a network-booted image's own entry.
-- For headless smoke tests: mame ... -autoboot_script emu/drive.lua
local presses = { 3, 25 }
local state = 0
local down = false
emu.register_frame(function()
    local t = manager.machine.time.seconds
    local port = manager.machine.ioport.ports[":KEYPAD3"]
    if port == nil then return end
    if not down and state < #presses and t >= presses[state + 1] then
        emu.print_info("drive.lua: pressing '1' (t=" .. t .. ")")
        port:field(0x10):set_value(1)
        down = true
    elseif down and t >= presses[state + 1] + 2 then
        emu.print_info("drive.lua: releasing '1'")
        port:field(0x10):clear_value()
        down = false
        state = state + 1
    end
end)
