-- Walk CONFIG through a screen and snapshot it. KEYS is a comma-separated
-- script of field names to press once the program reaches state WAIT_STATE.
local VSTATE = 0x8006
local WAIT = tonumber(os.getenv("WAIT_STATE") or "4")
local KEYS = os.getenv("KEYS") or ""
local SHOT = tonumber(os.getenv("SHOT_AFTER") or "600")

local seq = {}
for k in KEYS:gmatch("[^,]+") do seq[#seq + 1] = k end

local sp, n, timer, i = nil, 0, 0, 1
local phase = "wait"

local function port(f)
    if f:match("^P1") then return ":RIGHT_C" end
    return ":PANEL"
end

_G._st = emu.add_machine_frame_notifier(function()
    n = n + 1
    sp = sp or manager.machine.devices[":maincpu"].spaces["program"]
    timer = timer + 1

    if phase == "wait" then
        if sp:readv_u8(VSTATE) == WAIT then
            if timer > 300 then phase, timer = "keys", 0 end
        else
            timer = 0
        end
        if n > 30000 then print("TOUR: FAIL -- never reached state"); manager.machine:exit() end
    elseif phase == "keys" then
        if i > #seq then
            phase, timer = "settle", 0
        elseif timer == 1 then
            manager.machine.ioport.ports[port(seq[i])].fields[seq[i]]:set_value(1)
        elseif timer == 8 then
            manager.machine.ioport.ports[port(seq[i])].fields[seq[i]]:set_value(0)
        elseif timer >= 20 then
            i = i + 1; timer = 0
        end
    elseif phase == "settle" then
        if timer == SHOT then
            print(string.format("TOUR: snapshot at state %02X", sp:readv_u8(VSTATE)))
            manager.machine.video:snapshot()
        elseif timer > SHOT + 20 then
            manager.machine:exit()
        end
    end
end)
