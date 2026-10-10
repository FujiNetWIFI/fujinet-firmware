-- drive.lua: headless CONFIG smoke test -- walk the browser and boot.
--
-- Drives fujicfg (or the real CONFIG) through the P1 keypad: Enter to open
-- host 0, `DRIVE_DOWN` presses of '8' to move the cursor, then Enter to
-- boot the selected entry. Confirms the swap happened (the 'F','N' magic
-- pair at 2C09/2C0A vanishes) and prints one line:
--   drive: PASS | drive: FAIL <why>
--
-- Keypad ioports (see arcadia.cpp INPUT_PORTS): ':controller1_col2' field
-- 0x02 = '8' (down), ':controller1_col3' field 0x01 = Enter.
--
--   DRIVE_DOWN     cursor-down presses before boot (default 5 -> jungler on
--                  an SD whose listing is FujiNet/, <synthetic>, 5card,
--                  astrotest, fujibank, fujitest, jungler, ...)
--   DRIVE_TIMEOUT  emulated-seconds budget (default 20)

local downs = tonumber(os.getenv("DRIVE_DOWN") or "6")
local timeout = tonumber(os.getenv("DRIVE_TIMEOUT") or "30")

local step = 0
local nextt = 2.5           -- let the host page finish loading first
local pressed_down = 0
local finished = false

local function done(msg)
    print("drive: " .. msg)
    finished = true
    manager.machine:exit()
end

emu.register_frame(function()
    if finished then return end
    local mem = manager.machine.devices[":maincpu"].spaces["program"]
    local col2 = manager.machine.ioport.ports[":controller1_col2"]
    local col3 = manager.machine.ioport.ports[":controller1_col3"]
    if col2 == nil or col3 == nil then return done("FAIL no keypad ports") end
    local t = manager.machine.time.seconds
    if t >= timeout then return done("FAIL timeout at step " .. step) end
    if t < nextt then return end

    -- script: Enter(open host), downs x '8', Enter(boot); each = 0.3s down,
    -- 0.5s up, generous against the redraw bursts between key polls.
    if step == 0 then col3:field(0x01):set_value(1); nextt = t + 0.3; step = 1
    elseif step == 1 then col3:field(0x01):set_value(0); nextt = t + 1.2; step = 2
    elseif step == 2 then
        if pressed_down < downs then
            col2:field(0x02):set_value(1); nextt = t + 0.3; step = 3
        else step = 5; nextt = t + 0.3 end
    elseif step == 3 then
        col2:field(0x02):set_value(0); nextt = t + 0.5; pressed_down = pressed_down + 1
        step = 2
    elseif step == 5 then col3:field(0x01):set_value(1); nextt = t + 0.3; step = 6
    elseif step == 6 then col3:field(0x01):set_value(0); nextt = t + 3.0; step = 7
    elseif step == 7 then
        local swapped = mem:read_u8(0x2C09) ~= 0x46 or mem:read_u8(0x2C0A) ~= 0x4E
        if swapped then return done("PASS")
        else return done(string.format("FAIL no swap (boot state %02X err %02X)",
                                       mem:read_u8(0x2C06), mem:read_u8(0x2C08))) end
    end
end)
