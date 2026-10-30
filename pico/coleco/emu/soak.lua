-- soak.lua -- boot one image over the network and compare the served window byte for byte
-- with tools/expect.py's.
--
--   SOAK_EXPECT   path to the expected 32K window (soak.sh precomputes it)
--   SOAK_TIMEOUT  emulated seconds to wait for the swap (default 40)
--
-- Reads use readv_u8: read_u8 has side effects and would fire bank hotspots mid-dump. The
-- swap is judged first, by the magic vanishing; after it the mailbox offsets are the game's.

local EXPECT  = os.getenv("SOAK_EXPECT")
local TIMEOUT = tonumber(os.getenv("SOAK_TIMEOUT") or "40")

local function verdict(msg)
    emu.print_info("soak: " .. msg)
    manager.machine:exit()
end

if _G.soak_state == nil then _G.soak_state = { done = false } end
local run = _G.soak_state

_G.soak_sub = emu.add_machine_frame_notifier(function ()
    if run.done then return end

    local mem = manager.machine.devices[":maincpu"].spaces["program"]
    local now = manager.machine.time.seconds

    -- $FC09/$FC0A are 'F','N' while the mailbox is live. A booted game owns
    -- those bytes, so their disappearance IS the swap.
    local swapped = mem:readv_u8(0xFC09) ~= 0x46 or mem:readv_u8(0xFC0A) ~= 0x4E

    if not swapped then
        if now >= TIMEOUT then
            run.done = true
            verdict(string.format("FAIL no swap after %ds (BOOT_STATE=%02X "
                .. "BOOT_ERR=%02X ERR=%02X)", TIMEOUT,
                mem:readv_u8(0xFC06), mem:readv_u8(0xFC08), mem:readv_u8(0xFC02)))
        end
        return
    end

    run.done = true

    local handle = io.open(EXPECT, "rb")
    if handle == nil then return verdict("FAIL cannot open " .. tostring(EXPECT)) end
    local want = handle:read("*a")
    handle:close()
    if #want ~= 0x8000 then
        return verdict(string.format("FAIL expected file is %d bytes", #want))
    end

    for off = 0, 0x7FFF do
        local got = mem:readv_u8(0x8000 + off)
        local exp = want:byte(off + 1)
        if got ~= exp then
            return verdict(string.format(
                "FAIL window differs at %04X: got %02X want %02X",
                0x8000 + off, got, exp))
        end
    end
    verdict("PASS")
end)
