-- bankprobe.lua -- M8: check fujibank.bin's APPBANK results and the tick.
--
-- fujibank.asm leaves what it read through the page window in RAM at $8010
-- (see its header). This waits for its done marker, checks each byte against
-- what the spec in fuji_mailbox.h says it must be, then measures the tick
-- against ten seconds of emulated time. Against time, not frames: the console
-- refreshes at 60.53 Hz (4.09 MHz / 256 x 264), so 600 frames is 595 ticks.
--
--   FUJINET_TCP=127.0.0.1:9 ./run.sh fujibank bankprobe
local VRES = 0x8010
local SPAN = 605                 -- frames: about ten seconds
local n, t0, f0, s0, sp, fails = 0, nil, nil, nil, nil, 0

local function now() return manager.machine.time:as_double() end

local function tick()
    -- HI, LO, HI as the client does; reads of the status page are inert
    while true do
        local h1 = sp:readv_u8(0xFC0F)
        local lo = sp:readv_u8(0xFC0E)
        local h2 = sp:readv_u8(0xFC0F)
        if h1 == h2 then return h1 * 256 + lo end
    end
end

local function check(what, got, want)
    local ok = got == want
    if not ok then fails = fails + 1 end
    print(string.format("BANKPROBE: %-28s $%02X (want $%02X) %s",
                        what, got, want, ok and "ok" or "FAIL"))
end

_G._bp = emu.add_machine_frame_notifier(function()
    n = n + 1
    sp = sp or manager.machine.devices[":maincpu"].spaces["program"]
    if not t0 then
        if sp:readv_u8(VRES + 3) ~= 0x5A then
            if n > 3000 then
                print("BANKPROBE: FAIL -- fujibank never finished its page tests")
                manager.machine:exit()
            end
            return
        end
        check("page 0 id at $4800", sp:readv_u8(VRES + 0), 0xA0)
        check("page 1 id at $4800", sp:readv_u8(VRES + 1), 0xA1)
        check("protocol version", sp:readv_u8(VRES + 2), 0x02)
        check("missing page 2 reads", sp:readv_u8(VRES + 4), 0xFF)
        check("window now (page 1)", sp:readv_u8(0x4800), 0xA1)
        check("$6800 unpopulated", sp:readv_u8(0x6800), 0xFF)
        t0, f0, s0 = tick(), n, now()
        return
    end
    if n - f0 == 60 then manager.machine.video:snapshot() end
    if n - f0 < SPAN then return end
    local d = (tick() - t0) % 65536
    local secs = now() - s0
    local want = secs * 60
    local ok = math.abs(d - want) <= 1.5
    if not ok then fails = fails + 1 end
    print(string.format("BANKPROBE: tick advanced %d in %.3f s (want %.1f) %s",
                        d, secs, want, ok and "ok" or "FAIL"))
    print(fails == 0 and "BANKPROBE: PASS" or "BANKPROBE: FAIL")
    manager.machine:exit()
end)
