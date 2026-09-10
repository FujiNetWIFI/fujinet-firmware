-- Drive CONFIG from power-on to a booted cartridge: host list -> open host 0
-- -> walk the directory -> fire -> byte-compare what booted.
--
-- Every wait is on the program's own state in the RAM arena, never on a frame
-- number, so a slow socket round trip cannot turn into a flake.
local ST_HOST, ST_FILE = 4, 5
local VSTATE, VCUR, VNROW = 0x8006, 0x8007, 0x8008

local WANT = os.getenv("BOOT_IMAGE") or ""
local PICK = tonumber(os.getenv("PICK_ROW") or "6")

local sp, n, timer, moves = nil, 0, 0, 0
local phase = "hosts"
local lastRows, stable = -1, 0

local function press(f, p) manager.machine.ioport.ports[p].fields[f]:set_value(1) end
local function release(f, p) manager.machine.ioport.ports[p].fields[f]:set_value(0) end

local function verify()
    local f = io.open(WANT, "rb")
    if not f then print("CFGDRIVE: FAIL -- cannot open " .. WANT); return end
    local img = f:read("*a"); f:close()
    local bad = 0
    for i = 1, #img do
        if sp:readv_u8(0x0800 + i - 1) ~= img:byte(i) then bad = bad + 1 end
    end
    print(bad == 0
        and string.format("CFGDRIVE: PASS -- booted %d bytes, byte-identical", #img)
        or  string.format("CFGDRIVE: FAIL -- %d of %d bytes differ", bad, #img))
end

_G._cd = emu.add_machine_frame_notifier(function()
    n = n + 1
    sp = sp or manager.machine.devices[":maincpu"].spaces["program"]
    timer = timer + 1
    local st = sp:readv_u8(VSTATE)

    if phase == "hosts" then
        if st == ST_HOST and sp:readv_u8(VNROW) == 0 then
            -- the host screen does not fill VNROW; settle on the state itself
            if timer > 240 then
                print("CFGDRIVE: host list up, opening slot 0")
                phase, timer = "openhost", 0
            end
        else
            timer = 0
        end
        if n > 20000 then print("CFGDRIVE: FAIL -- no host list"); manager.machine:exit() end

    elseif phase == "openhost" then
        if timer == 1 then press("P1 Push Down", ":RIGHT_C")
        elseif timer == 6 then release("P1 Push Down", ":RIGHT_C")
        elseif timer > 6 then phase, timer = "files", 0 end

    elseif phase == "files" then
        local rows = sp:readv_u8(VNROW)
        if rows == lastRows then stable = stable + 1 else stable, lastRows = 0, rows end
        if st == ST_FILE and rows > PICK and stable > 240 then
            print(string.format("CFGDRIVE: listing has %d rows, walking to row %d", rows, PICK))
            phase, timer = "move", 0
        elseif n > 30000 then
            print("CFGDRIVE: FAIL -- no directory listing")
            manager.machine:exit()
        end

    elseif phase == "move" then
        if moves >= PICK then
            phase, timer = "fire", 0
        elseif timer == 1 then press("P1 Down", ":RIGHT_C")
        elseif timer == 6 then release("P1 Down", ":RIGHT_C")
        elseif timer >= 14 then moves = moves + 1; timer = 0 end

    elseif phase == "fire" then
        if timer == 1 then
            if sp:readv_u8(VCUR) ~= PICK then
                print(string.format("CFGDRIVE: FAIL -- cursor on row %d, wanted %d",
                                    sp:readv_u8(VCUR), PICK))
                manager.machine:exit(); return
            end
            press("P1 Push Down", ":RIGHT_C")
        elseif timer == 6 then release("P1 Push Down", ":RIGHT_C")
        elseif timer > 6 then phase, timer = "boot", 0 end

    elseif phase == "boot" then
        if timer % 60 == 0 and sp:readv_u8(0x47FC) ~= 0x46 then
            verify(); manager.machine:exit()
        elseif timer > 12000 then
            print("CFGDRIVE: FAIL -- never swapped"); verify(); manager.machine:exit()
        end
    end
end)
