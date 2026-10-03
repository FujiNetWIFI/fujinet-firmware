-- Move the name-entry cursor with the hand controller, not the console panel.
--
-- Left and right on the stick are the natural way across a 16-wide keyboard,
-- and INSCAN did not decode them: the cursor only ever moved on TIME/HOLD.
-- This walks right three cells, types, walks back one, types again, and checks
-- both the cursor and the accumulator -- the panel buttons are never touched.
local ST_EDIT = 1
local VSTATE, VEDX, VEDLEN, VENTRY = 0x8006, 0x801C, 0x801E, 0x8080

-- "abcdefghijklmnop" is row 0, so x=3 is 'd' and x=2 is 'c'.
local seq = {
    {f = "P1 Right",     x = 1},
    {f = "P1 Right",     x = 2},
    {f = "P1 Right",     x = 3},
    {f = "P1 Push Down", x = 3, entry = "d"},
    {f = "P1 Left",      x = 2},
    {f = "P1 Push Down", x = 2, entry = "dc"},
}

local sp, n, timer, i, phase, fails = nil, 0, 0, 1, "wait", 0

local function entry()
    local s = ""
    for k = 0, 7 do
        local c = sp:readv_u8(VENTRY + k)
        if c == 0 then break end
        s = s .. string.char(c)
    end
    return s
end

_G._es = emu.add_machine_frame_notifier(function()
    n = n + 1
    sp = sp or manager.machine.devices[":maincpu"].spaces["program"]
    timer = timer + 1

    if phase == "wait" then
        if sp:readv_u8(VSTATE) == ST_EDIT and timer > 240 then
            print("EDSTICK: keyboard up, driving it from the stick")
            phase, timer = "keys", 0
        elseif sp:readv_u8(VSTATE) ~= ST_EDIT then timer = 0 end
        if n > 20000 then print("EDSTICK: FAIL -- no keyboard"); manager.machine:exit() end

    elseif phase == "keys" then
        local s = seq[i]
        if s == nil then phase, timer = "done", 0
        elseif timer == 1 then
            manager.machine.ioport.ports[":RIGHT_C"].fields[s.f]:set_value(1)
        elseif timer == 10 then
            manager.machine.ioport.ports[":RIGHT_C"].fields[s.f]:set_value(0)
        elseif timer >= 40 then
            local x, e = sp:readv_u8(VEDX), entry()
            if x ~= s.x then
                fails = fails + 1
                print(string.format("EDSTICK: %-13s cursor x=%d, wanted %d", s.f, x, s.x))
            end
            if s.entry and e ~= s.entry then
                fails = fails + 1
                print(string.format("EDSTICK: %-13s entry %q, wanted %q", s.f, e, s.entry))
            end
            i, timer = i + 1, 0
        end

    elseif phase == "done" then
        if timer == 1 then
            print(string.format("EDSTICK: accumulator = %q (len %d)", entry(),
                                sp:readv_u8(VEDLEN)))
            print(fails == 0 and "EDSTICK: PASS -- stick moves the cursor both ways"
                              or string.format("EDSTICK: FAIL -- %d checks", fails))
            manager.machine.video:snapshot()
        elseif timer > 30 then manager.machine:exit() end
    end
end)
