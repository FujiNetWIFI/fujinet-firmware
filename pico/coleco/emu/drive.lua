-- drive.lua -- drive the CONFIG client headlessly.
--
--   DRIVE_SCRIPT   comma-separated actions, e.g. "fire,down,down,fire"
--                  fire up down left right k0..k9 star hash wait
--                  (`wait` just burns a beat, for screens that fetch)
--   DRIVE_HOST     with no DRIVE_SCRIPT: which host slot to open (default 1)
--   DRIVE_DOWN     with no DRIVE_SCRIPT: cursor moves before booting
--   DRIVE_EXPECT   substring that must appear on screen at the end; when set,
--                  the verdict is that rather than "a boot happened"
--   DRIVE_TIMEOUT  emulated seconds to give up after (default 60)
--   DRIVE_DUMP     set to always dump the screen at the end
--
-- Time comes from attotime:as_double(); machine.time.seconds is whole seconds only, which
-- held presses long enough to auto-repeat. Each press is held a beat and released, since
-- OS7's POLLER debounces.

local SCRIPT  = os.getenv("DRIVE_SCRIPT")
local HOST    = tonumber(os.getenv("DRIVE_HOST") or "1")
local DOWN    = tonumber(os.getenv("DRIVE_DOWN") or "0")
local EXPECT  = os.getenv("DRIVE_EXPECT")
local TIMEOUT = tonumber(os.getenv("DRIVE_TIMEOUT") or "60")
local DUMP    = os.getenv("DRIVE_DUMP")

-- coleco.cpp's own ports, not the controller slot device's: the driver carries
-- its own inputs and only routes them through the slot.
local JOY = { up = 0x01, right = 0x02, down = 0x04, left = 0x08, fire = 0x40 }
local PAD = { k0 = 0x0001, k1 = 0x0002, k2 = 0x0004, k3 = 0x0008,
              k4 = 0x0010, k5 = 0x0020, k6 = 0x0040, k7 = 0x0080,
              k8 = 0x0100, k9 = 0x0200, hash = 0x0400, star = 0x0800 }

local HOLD   = 0.30
local GAP    = 0.45
local SETTLE = 3.0

local function find_port(suffix)
    for tag, port in pairs(manager.machine.ioport.ports) do
        if tag:sub(-#suffix) == suffix then return port end
    end
    error("drive.lua: no input port ending in " .. suffix)
end

local script = {}
if SCRIPT then
    for step_name in SCRIPT:gmatch("[^,%s]+") do script[#script + 1] = step_name end
else
    for count = 1, HOST - 1 do script[#script + 1] = "down" end
    script[#script + 1] = "fire"
    for count = 1, DOWN do script[#script + 1] = "down" end
    script[#script + 1] = "fire"
end

if _G.drive == nil then
    _G.drive = { step = 0, t_next = SETTLE, down = false, done = false }
end
local run = _G.drive

local function press(action, on)
    if action == "wait" then return end
    local bit, port
    if JOY[action] then
        bit, port = JOY[action], find_port("STD_JOY1")
    elseif PAD[action] then
        bit, port = PAD[action], find_port("STD_KEYPAD1")
    else
        error("drive.lua: unknown action '" .. action .. "'")
    end
    local input = port:field(bit)
    if input == nil then
        error(string.format("drive.lua: no field %#x on that port", bit))
    end
    -- set_value takes the LOGICAL state: MAME inverts IP_ACTIVE_LOW for you.
    if on then input:set_value(1) else input:clear_value() end
end

-- A selected row is drawn from the inverse charset at +0x60, so decode it back
-- or the cursor row reads as blanks.
local function screen_lines()
    local vdp = manager.machine.devices[":tms9928a"]
    local vram = vdp and vdp.spaces["vram"]
    if vram == nil then return nil end
    local lines = {}
    for row = 0, 23 do
        local chars, inv = {}, false
        for col = 0, 31 do
            local byte = vram:read_u8(0x1800 + row * 32 + col)
            if byte >= 0x80 and byte <= 0xDF then byte = byte - 0x60; inv = true end
            if byte < 32 or byte > 126 then byte = 32 end
            chars[#chars + 1] = string.char(byte)
        end
        lines[#lines + 1] = (inv and "#" or "|") .. table.concat(chars)
    end
    return lines
end

local function dump()
    local lines = screen_lines()
    if lines then
        for idx, line in ipairs(lines) do emu.print_info("drive" .. line) end
    end
end

local function finish(verdict)
    run.done = true
    if DUMP or verdict:sub(1, 4) == "FAIL" then dump() end
    emu.print_info("drive: " .. verdict)
    manager.machine:exit()
end

_G.drive_sub = emu.add_machine_frame_notifier(function ()
    if run.done then return end

    local now = manager.machine.time:as_double()
    local mem = manager.machine.devices[":maincpu"].spaces["program"]

    -- With no expectation, the verdict is the swap: $FC09/$FC0A are 'F','N'
    -- only while the mailbox is live, and a booted game owns those bytes.
    if not EXPECT then
        if mem:readv_u8(0xFC09) ~= 0x46 or mem:readv_u8(0xFC0A) ~= 0x4E then
            if run.swapped == nil then run.swapped = now + 2.0; return end
            if now < run.swapped then return end
            -- Raw name-table bytes as ASCII: readable only while an OS7 client
            -- is up. A booted game has its own tiles, so gibberish here IS the
            -- evidence that something other than CONFIG is drawing.
            if DUMP then dump() end
            return finish("PASS")
        end
    elseif run.step >= #script and not run.down then
        local lines = screen_lines()
        if lines then
            local joined = table.concat(lines, "\n")
            if joined:find(EXPECT, 1, true) then return finish("PASS") end
        end
    end

    if now >= TIMEOUT then
        return finish(string.format(
            "FAIL after %ds (step %d of %d, BOOT_STATE=%02X)",
            TIMEOUT, run.step, #script, mem:readv_u8(0xFC06)))
    end

    if now < run.t_next then return end

    if run.down then
        press(script[run.step], false)
        run.down = false
        run.t_next = now + GAP
        return
    end

    if run.step >= #script then return end

    run.step = run.step + 1
    emu.print_info(string.format("drive: %s (t=%.1f)", script[run.step], now))
    press(script[run.step], true)
    run.down = true
    run.t_next = now + HOLD
end)
