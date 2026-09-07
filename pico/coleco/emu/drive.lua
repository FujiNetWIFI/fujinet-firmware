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
-- Timing is read with attotime:as_double(), NOT machine.time.seconds -- that is
-- the whole-seconds FIELD, and comparing against it quantises every interval to
-- a full second, which held presses long enough for auto-repeat to walk the
-- cursor several rows per "press".
--
-- Presses are held for a beat and released, because OS7's POLLER debounces: a
-- press that never lets go is one event, not many.

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
    for a in SCRIPT:gmatch("[^,%s]+") do script[#script + 1] = a end
else
    for _ = 1, HOST - 1 do script[#script + 1] = "down" end
    script[#script + 1] = "fire"
    for _ = 1, DOWN do script[#script + 1] = "down" end
    script[#script + 1] = "fire"
end

if _G.drive == nil then
    _G.drive = { step = 0, t_next = SETTLE, down = false, done = false }
end
local d = _G.drive

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
    local f = port:field(bit)
    if f == nil then
        error(string.format("drive.lua: no field %#x on that port", bit))
    end
    -- set_value takes the LOGICAL state: MAME inverts IP_ACTIVE_LOW for you.
    if on then f:set_value(1) else f:clear_value() end
end

-- A selected row is drawn from the inverse charset at +0x60, so decode it back
-- or the cursor row reads as blanks.
local function screen_lines()
    local vdp = manager.machine.devices[":tms9928a"]
    local vram = vdp and vdp.spaces["vram"]
    if vram == nil then return nil end
    local lines = {}
    for row = 0, 23 do
        local c, inv = {}, false
        for col = 0, 31 do
            local b = vram:read_u8(0x1800 + row * 32 + col)
            if b >= 0x80 and b <= 0xDF then b = b - 0x60; inv = true end
            if b < 32 or b > 126 then b = 32 end
            c[#c + 1] = string.char(b)
        end
        lines[#lines + 1] = (inv and "#" or "|") .. table.concat(c)
    end
    return lines
end

local function dump()
    local lines = screen_lines()
    if lines then
        for _, l in ipairs(lines) do emu.print_info("drive" .. l) end
    end
end

local function finish(verdict)
    d.done = true
    if DUMP or verdict:sub(1, 4) == "FAIL" then dump() end
    emu.print_info("drive: " .. verdict)
    manager.machine:exit()
end

_G.drive_sub = emu.add_machine_frame_notifier(function ()
    if d.done then return end

    local t = manager.machine.time:as_double()
    local sp = manager.machine.devices[":maincpu"].spaces["program"]

    -- With no expectation, the verdict is the swap: $FC09/$FC0A are 'F','N'
    -- only while the mailbox is live, and a booted game owns those bytes.
    if not EXPECT then
        if sp:readv_u8(0xFC09) ~= 0x46 or sp:readv_u8(0xFC0A) ~= 0x4E then
            if d.swapped == nil then d.swapped = t + 2.0; return end
            if t < d.swapped then return end
            -- Raw name-table bytes as ASCII: readable only while an OS7 client
            -- is up. A booted game has its own tiles, so gibberish here IS the
            -- evidence that something other than CONFIG is drawing.
            if DUMP then dump() end
            return finish("PASS")
        end
    elseif d.step >= #script and not d.down then
        local lines = screen_lines()
        if lines then
            local joined = table.concat(lines, "\n")
            if joined:find(EXPECT, 1, true) then return finish("PASS") end
        end
    end

    if t >= TIMEOUT then
        return finish(string.format(
            "FAIL after %ds (step %d of %d, BOOT_STATE=%02X)",
            TIMEOUT, d.step, #script, sp:readv_u8(0xFC06)))
    end

    if t < d.t_next then return end

    if d.down then
        press(script[d.step], false)
        d.down = false
        d.t_next = t + GAP
        return
    end

    if d.step >= #script then return end

    d.step = d.step + 1
    emu.print_info(string.format("drive: %s (t=%.1f)", script[d.step], t))
    press(script[d.step], true)
    d.down = true
    d.t_next = t + HOLD
end)
