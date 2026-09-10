-- Type into CONFIG's on-screen keyboard and check the accumulator.
local VSTATE, VENTRY = 0x8006, 0x8280
local seq = {"MODE (Button 3)",
             "P1 Push Down","P1 Push Down","P1 Push Down"}
local sp, n, timer, i, phase = nil, 0, 0, 1, "wait"
local function port(f) return f:match("^P1") and ":RIGHT_C" or ":PANEL" end
_G._et = emu.add_machine_frame_notifier(function()
    n = n + 1
    sp = sp or manager.machine.devices[":maincpu"].spaces["program"]
    timer = timer + 1
    if phase == "wait" then
        if sp:readv_u8(VSTATE) == 4 and timer > 300 then phase, timer = "keys", 0
        elseif sp:readv_u8(VSTATE) ~= 4 then timer = 0 end
        if n > 30000 then print("ET: FAIL -- no host list"); manager.machine:exit() end
    elseif phase == "keys" then
        if i > #seq then phase, timer = "check", 0
        elseif timer == 1 then manager.machine.ioport.ports[port(seq[i])].fields[seq[i]]:set_value(1)
        elseif timer == 10 then manager.machine.ioport.ports[port(seq[i])].fields[seq[i]]:set_value(0)
        elseif timer >= 40 then i = i + 1; timer = 0 end
    elseif phase == "check" then
        if timer > 120 then
            local s = ""
            for k = 0, 7 do
                local c = sp:readv_u8(VENTRY + k)
                if c == 0 then break end
                s = s .. string.char(c)
            end
            print(string.format("ET: accumulator = %q (len %d)", s, sp:readv_u8(0x8015)))
            print(s == "aaa" and "ET: PASS -- three keystrokes landed"
                              or "ET: FAIL -- keystrokes lost")
            manager.machine.video:snapshot()
            phase, timer = "done", 0
        end
    elseif phase == "done" then
        if timer > 30 then manager.machine:exit() end
    end
end)
