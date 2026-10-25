-- bankprobe.lua: check testrom/fujibank.asm's results, headless.
--
-- fujibank selects every page, calls into one, pokes and reads back both
-- halves of cart RAM, makes stray reads after a closed stream, and times the
-- tick over 60 vblanks; it leaves its results at 18D0H-18DFH (5AH at 18DFH
-- when done). This checks them, then watches the tick against emulated
-- frames for 600 more. Prints exactly one line:
--   bankprobe: PASS | bankprobe: FAIL <why>
--
--   BANKPROBE_TIMEOUT  frames to wait for the ROM (default 600)

local timeout = tonumber(os.getenv("BANKPROBE_TIMEOUT") or "600")
local frames = 0
local t0, f0
local finished = false

local function done(msg)
    print("bankprobe: " .. msg)
    finished = true
    manager.machine:exit()
end

local function tick(mem)
    return mem:read_u8(0x2C0E) | (mem:read_u8(0x2C0F) << 8)
end

local want = {
    [0] = 0x00, 0x01, 0x02, 0x03, 0xFF,  -- page ids, page 4 missing
    0xC2,                                 -- page 2's entry
    0x01, 0x01, 0x01,                     -- RAM-L, RAM-H, strays
    0x02, 0x00,                           -- version, F/N
}
local names = { [0] = "page 0", "page 1", "page 2", "page 3", "missing page",
    "page 2 entry", "RAM-L", "RAM-H", "strays", "version", "magic" }

-- add_machine_frame_notifier, not the removed emu.register_frame; the handle
-- is kept in a global or it is collected and the callback stops.
_G.BANKPROBE_SUB = emu.add_machine_frame_notifier(function()
    if finished then return end
    frames = frames + 1
    local mem = manager.machine.devices[":maincpu"].spaces["program"]
    if t0 == nil then
        if frames > timeout then
            return done(string.format("FAIL no result (18DF=%02X)", mem:read_u8(0x18DF)))
        end
        if mem:read_u8(0x18DF) ~= 0x5A then return end
        for i = 0, #want do
            local got = mem:read_u8(0x18D0 + i)
            if got ~= want[i] then
                return done(string.format("FAIL %s: %02X, want %02X", names[i], got, want[i]))
            end
        end
        local a = mem:read_u8(0x18DB) | (mem:read_u8(0x18DC) << 8)
        local b = mem:read_u8(0x18DD) | (mem:read_u8(0x18DE) << 8)
        local d = (b - a) & 0xFFFF
        if d < 59 or d > 61 then
            return done(string.format("FAIL the ROM saw %d ticks in 60 vblanks", d))
        end
        t0, f0 = tick(mem), frames
        return
    end
    if frames - f0 < 600 then return end
    local d = (tick(mem) - t0) & 0xFFFF
    if d < 599 or d > 601 then
        return done(string.format("FAIL %d ticks in 600 frames", d))
    end
    return done("PASS")
end)
