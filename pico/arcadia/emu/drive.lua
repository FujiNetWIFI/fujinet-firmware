-- drive.lua: headless CONFIG smoke test. Enter opens host 0, DRIVE_DOWN presses of '8'
-- move the cursor, Enter boots; PASS when the 'F','N' magic at 2C09/2C0A vanishes.
--   drive: PASS | drive: FAIL <why>
--
-- Keypad: ':controller1_col2' field 0x02 = '8' (down), ':controller1_col3' 0x01 = Enter.
--
--   DRIVE_DOWN     cursor-down presses before boot (default 6)
--   DRIVE_TIMEOUT  emulated-seconds budget (default 30)

local downs = tonumber(os.getenv("DRIVE_DOWN") or "6")
local timeout = tonumber(os.getenv("DRIVE_TIMEOUT") or "30")

local step = 0
local frames = 0
local nextt = 2.5           -- let the host page finish loading first
local pressed_down = 0
local finished = false

local function done(msg)
    print("drive: " .. msg)
    finished = true
    manager.machine:exit()
end

-- Keep the handle in a global or it is collected and the callback stops.
_G.DRIVE_SUB = emu.add_machine_frame_notifier(function()
    if finished then return end
    local mem = manager.machine.devices[":maincpu"].spaces["program"]
    local col2 = manager.machine.ioport.ports[":controller1_col2"]
    local col3 = manager.machine.ioport.ports[":controller1_col3"]
    if col2 == nil or col3 == nil then return done("FAIL no keypad ports") end
    -- machine.time.seconds is whole seconds only; count 60 Hz frames instead.
    frames = frames + 1
    local seconds = frames / 60
    if seconds >= timeout then return done("FAIL timeout at step " .. step) end
    if seconds < nextt then return end

    -- 0.3 s down, 0.5 s up: generous against the redraw bursts between key polls.
    if step == 0 then col3:field(0x01):set_value(1); nextt = seconds + 0.3; step = 1
    elseif step == 1 then col3:field(0x01):set_value(0); nextt = seconds + 1.2; step = 2
    elseif step == 2 then
        if pressed_down < downs then
            col2:field(0x02):set_value(1); nextt = seconds + 0.3; step = 3
        else step = 5; nextt = seconds + 0.3 end
    elseif step == 3 then
        col2:field(0x02):set_value(0); nextt = seconds + 0.5; pressed_down = pressed_down + 1
        step = 2
    elseif step == 5 then col3:field(0x01):set_value(1); nextt = seconds + 0.3; step = 6
    elseif step == 6 then col3:field(0x01):set_value(0); nextt = seconds + 3.0; step = 7
    elseif step == 7 then
        local swapped = mem:read_u8(0x2C09) ~= 0x46 or mem:read_u8(0x2C0A) ~= 0x4E
        if swapped then return done("PASS")
        else return done(string.format("FAIL no swap (boot state %02X err %02X)",
                                       mem:read_u8(0x2C06), mem:read_u8(0x2C08))) end
    end
end)
