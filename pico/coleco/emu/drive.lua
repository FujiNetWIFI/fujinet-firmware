-- drive.lua -- drive fujicfg headlessly: pick a host, walk down the listing,
-- boot what the cursor is on. The verdict is the mailbox magic disappearing,
-- because that is the one thing only a real swap can cause.
--
--   DRIVE_HOST     which host slot to open, 1-based (default 1)
--   DRIVE_DOWN     cursor presses before booting (default 0)
--   DRIVE_TIMEOUT  emulated seconds to give up after (default 60)
--
-- Timing is in EMULATED seconds so any throttle setting works. Presses are
-- held for a beat and released, because OS7's POLLER debounces: a press that
-- never lets go is one event, not many.

local HOST    = tonumber(os.getenv("DRIVE_HOST") or "1")
local DOWN    = tonumber(os.getenv("DRIVE_DOWN") or "0")
local TIMEOUT = tonumber(os.getenv("DRIVE_TIMEOUT") or "60")

-- Controller bits, from coleco.cpp's own STD_JOY1 port. Active low.
--
-- NOT the slot device's COMMON0/KEYPAD: the ColecoVision driver carries its
-- own input ports and only routes them through the controller slot, so the
-- tags a script has to drive are :STD_JOY1 and :STD_KEYPAD1.
local JOY_DOWN = 0x04
local BTN_FIRE = 0x40

local HOLD    = 0.30        -- seconds a press is held
local GAP     = 0.45        -- seconds between presses
local SETTLE  = 3.0         -- let the host page finish its transaction first

-- Find by suffix rather than hard-coding a path a MAME rename would silently
-- break -- and complain loudly if it is missing, because a script that presses
-- nothing looks exactly like a client that ignores input.
local function find_port(suffix)
    for tag, port in pairs(manager.machine.ioport.ports) do
        if tag:sub(-#suffix) == suffix then return port end
    end
    error("drive.lua: no input port ending in " .. suffix)
end

if _G.drive == nil then
    _G.drive = { step = 0, t_next = SETTLE, down = false, done = false }
end
local d = _G.drive

-- The script: open a host, walk down, boot.
local script = {}
table.insert(script, { kind = "fire" })                 -- enter the host
for _ = 1, HOST - 1 do
    table.insert(script, 1, { kind = "down" })          -- ...after moving to it
end
for _ = 1, DOWN do
    table.insert(script, { kind = "down" })
end
table.insert(script, { kind = "fire" })                 -- boot

local function press(kind, on)
    local port = find_port("STD_JOY1")
    local bit = (kind == "fire") and BTN_FIRE or JOY_DOWN
    local f = port:field(bit)

    if f == nil then
        error(string.format("drive.lua: STD_JOY1 has no field %#x", bit))
    end
    -- set_value takes the LOGICAL state, not the electrical one: MAME inverts
    -- an IP_ACTIVE_LOW field for you, so 1 is pressed.
    if on then f:set_value(1) else f:clear_value() end
end

_G.drive_sub = emu.add_machine_frame_notifier(function ()
    if d.done then return end

    local t = manager.machine.time.seconds
    local sp = manager.machine.devices[":maincpu"].spaces["program"]

    -- Did the swap happen? $FC09/$FC0A are 'F','N' only while the mailbox is
    -- live; a booted game owns those bytes.
    if sp:readv_u8(0xFC09) ~= 0x46 or sp:readv_u8(0xFC0A) ~= 0x4E then
        -- The swap happened. Give the booted image a couple of seconds to put
        -- something of its own on screen before declaring victory: a swap that
        -- lands on a broken image looks exactly like one that works, until you
        -- look at the screen.
        if d.swapped == nil then
            d.swapped = t + 2.0
            return
        end
        if t < d.swapped then return end
        d.done = true
        -- Raw pattern-name-table bytes rendered as ASCII. That reads as text
        -- only while an OS7 client with load_ascii() is up; a booted game has
        -- its own tiles, so expect gibberish -- gibberish IS the evidence that
        -- something other than CONFIG is now drawing.
        local vdp = manager.machine.devices[":tms9928a"]
        local vram = vdp and vdp.spaces["vram"]
        if vram then
            for row = 0, 23 do
                local chars = {}
                for col = 0, 31 do
                    local b = vram:read_u8(0x1800 + row * 32 + col)
                    if b < 32 or b > 126 then b = 32 end
                    chars[#chars + 1] = string.char(b)
                end
                emu.print_info("drive| " .. table.concat(chars))
            end
        end
        emu.print_info("drive: PASS")
        manager.machine:exit()
        return
    end

    if t >= TIMEOUT then
        d.done = true
        emu.print_info(string.format(
            "drive: FAIL no boot after %ds (step %d of %d, BOOT_STATE=%02X)",
            TIMEOUT, d.step, #script, sp:readv_u8(0xFC06)))
        -- Dump what the client was showing. A driver that presses the right
        -- buttons at the wrong screen looks identical to a client that ignores
        -- them, and the status line usually says which.
        local vdp = manager.machine.devices[":tms9928a"]
        local vram = vdp and vdp.spaces["vram"]
        if vram then
            for row = 0, 23 do
                local chars = {}
                for col = 0, 31 do
                    local b = vram:read_u8(0x1800 + row * 32 + col)
                    if b < 32 or b > 126 then b = 32 end
                    chars[#chars + 1] = string.char(b)
                end
                emu.print_info("drive| " .. table.concat(chars))
            end
        end
        manager.machine:exit()
        return
    end

    if t < d.t_next then return end

    if d.down then
        press(script[d.step].kind, false)
        d.down = false
        d.t_next = t + GAP
        return
    end

    if d.step >= #script then return end     -- pressed everything; wait it out

    d.step = d.step + 1
    emu.print_info(string.format("drive: %s (t=%.1f)", script[d.step].kind, t))
    press(script[d.step].kind, true)
    d.down = true
    d.t_next = t + HOLD
end)
