-- Descend into the first entry (a directory) and check the path and the new
-- listing. This is what exercises PATHAPP and the full-length re-read: the
-- name drawn on screen is crunched to the display width and is NOT a path.
local VNROW, VCUR, VPATH = 0x8007, 0x8006, 0x8100
local sp
local phase, timer, before = "wait", 0, nil
local n = 0
-- VNROW counts up as the rows arrive, so "nonzero" means the page is still
-- being built. Wait for it to stop moving instead, or the fire lands mid-page.
local lastRows, stable = -1, 0

local function path()
    local s = ""
    for i = 0, 63 do
        local c = sp:readv_u8(VPATH + i)
        if c == 0 then break end
        s = s .. string.char(c)
    end
    return s
end

_G._dt = emu.add_machine_frame_notifier(function()
    n = n + 1
    sp = sp or manager.machine.devices[":maincpu"].spaces["program"]
    timer = timer + 1

    if phase == "wait" then
        local rows = sp:readv_u8(VNROW)
        if rows == lastRows then stable = stable + 1 else stable, lastRows = 0, rows end
        if rows > 0 and stable > 180 then
            before = path()
            print(string.format("DIRTEST: at %q with %d rows", before, rows))
            phase, timer = "fire", 0
        elseif n > 12000 then
            print("DIRTEST: FAIL -- no listing")
            manager.machine:exit()
        end
    elseif phase == "fire" then
        if timer == 1 then
            manager.machine.ioport.ports[":RIGHT_C"].fields["P1 Push Down"]:set_value(1)
        elseif timer == 6 then
            manager.machine.ioport.ports[":RIGHT_C"].fields["P1 Push Down"]:set_value(0)
        elseif timer > 6 then
            phase, timer = "check", 0
        end
    elseif phase == "check" then
        local now = path()
        if now ~= before and sp:readv_u8(VNROW) > 0 then
            print(string.format("DIRTEST: descended to %q, %d rows", now, sp:readv_u8(VNROW)))
            if now == before .. "FujiNet/" then
                print("DIRTEST: PASS -- path appended and the new page listed")
            else
                print("DIRTEST: FAIL -- unexpected path")
            end
            manager.machine:exit()
        elseif timer > 9000 then
            print(string.format("DIRTEST: FAIL -- still at %q, %d rows", now, sp:readv_u8(VNROW)))
            manager.machine:exit()
        end
    end
end)
