-- M3 gate: browse the host's root, walk the cursor down to a chosen row, fire,
-- and prove the image that boots is byte-identical to the file on disk.
--
-- Nothing here is timed by guesswork: it waits on the client's own state in the
-- RAM arena (VNROW, then the served window) rather than on frame numbers, so a
-- slow socket round trip cannot turn into a flaky pass.
local WANT = os.getenv("BOOT_IMAGE") or ""
local PICK = tonumber(os.getenv("PICK_ROW") or "6")

local VNROW = 0x8007
local VCUR  = 0x8006

local sp
local phase, timer, moves = "wait", 0, 0
local n = 0
-- VNROW counts up as rows arrive, so "big enough" can be true while the page
-- is still building. Wait for it to stop moving, or the first press lands
-- mid-listing and the cursor ends up somewhere else.
local lastRows, stable = -1, 0

local function press(field, port)
    manager.machine.ioport.ports[port].fields[field]:set_value(1)
end
local function release(field, port)
    manager.machine.ioport.ports[port].fields[field]:set_value(0)
end

local function verify()
    local f = io.open(WANT, "rb")
    if not f then print("DRIVE: FAIL -- cannot open " .. WANT); return true end
    local img = f:read("*a"); f:close()
    local bad = 0
    for i = 1, #img do
        if sp:readv_u8(0x0800 + i - 1) ~= img:byte(i) then bad = bad + 1 end
    end
    if bad == 0 then
        print(string.format("DRIVE: PASS -- picked row %d and booted %d bytes, byte-identical",
                            PICK, #img))
    else
        print(string.format("DRIVE: FAIL -- %d of %d bytes differ", bad, #img))
    end
    return true
end

_G._drive = emu.add_machine_frame_notifier(function()
    n = n + 1
    sp = sp or manager.machine.devices[":maincpu"].spaces["program"]
    timer = timer + 1

    if phase == "wait" then
        local rows = sp:readv_u8(VNROW)
        if rows == lastRows then stable = stable + 1 else stable, lastRows = 0, rows end
        if rows > PICK and stable > 180 then
            print(string.format("DRIVE: listing has %d rows, walking to row %d", rows, PICK))
            phase, timer = "move", 0
        elseif n > 12000 then
            print("DRIVE: FAIL -- no directory listing appeared")
            manager.machine:exit()
        end

    elseif phase == "move" then
        if moves >= PICK then
            phase, timer = "fire", 0
        elseif timer == 1 then
            press("P1 Down", ":RIGHT_C")
        elseif timer == 6 then
            release("P1 Down", ":RIGHT_C")
        elseif timer >= 12 then
            moves = moves + 1
            timer = 0
        end

    elseif phase == "fire" then
        if timer == 1 then
            if sp:readv_u8(VCUR) ~= PICK then
                print(string.format("DRIVE: FAIL -- cursor is on row %d, wanted %d",
                                    sp:readv_u8(VCUR), PICK))
                manager.machine:exit()
                return
            end
            press("P1 Push Down", ":RIGHT_C")
        elseif timer == 6 then
            release("P1 Push Down", ":RIGHT_C")
        elseif timer > 6 then
            phase, timer = "boot", 0
        end

    elseif phase == "boot" then
        -- wait for the swap: the window stops being the client
        if timer > 4000 then
            verify()
            manager.machine:exit()
        elseif timer % 60 == 0 and sp:readv_u8(0x47FC) ~= 0x46 then
            -- the claim signature is gone, so the swap has happened
            verify()
            manager.machine:exit()
        end
    end
end)
