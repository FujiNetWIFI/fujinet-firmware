-- bankprobe.lua: check testrom/fujibank.asm's results at 18D0H-18DFH (5AH at 18DFH when
-- done), then check the tick against 600 more emulated frames.
--   bankprobe: PASS | bankprobe: FAIL <why>
--
--   BANKPROBE_TIMEOUT  frames to wait for the ROM (default 600)

local timeout = tonumber(os.getenv("BANKPROBE_TIMEOUT") or "600")
local frames = 0
local tick_start, frame_start
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

-- Keep the handle in a global or it is collected and the callback stops.
_G.BANKPROBE_SUB = emu.add_machine_frame_notifier(function()
    if finished then return end
    frames = frames + 1
    local mem = manager.machine.devices[":maincpu"].spaces["program"]
    if tick_start == nil then
        if frames > timeout then
            return done(string.format("FAIL no result (18DF=%02X)", mem:read_u8(0x18DF)))
        end
        if mem:read_u8(0x18DF) ~= 0x5A then return end
        for idx = 0, #want do
            local got = mem:read_u8(0x18D0 + idx)
            if got ~= want[idx] then
                return done(string.format("FAIL %s: %02X, want %02X", names[idx], got, want[idx]))
            end
        end
        local first = mem:read_u8(0x18DB) | (mem:read_u8(0x18DC) << 8)
        local second = mem:read_u8(0x18DD) | (mem:read_u8(0x18DE) << 8)
        local delta = (second - first) & 0xFFFF
        if delta < 59 or delta > 61 then
            return done(string.format("FAIL the ROM saw %d ticks in 60 vblanks", delta))
        end
        tick_start, frame_start = tick(mem), frames
        return
    end
    if frames - frame_start < 600 then return end
    local delta = (tick(mem) - tick_start) & 0xFFFF
    if delta < 599 or delta > 601 then
        return done(string.format("FAIL %d ticks in 600 frames", delta))
    end
    return done("PASS")
end)
