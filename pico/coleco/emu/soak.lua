-- soak.lua -- boot one image over the network and verify the served window
-- byte for byte against what colmap says the console should see.
--
--   SOAK_EXPECT   path to the expected 32K window (soak.sh precomputes it)
--   SOAK_TIMEOUT  emulated seconds to wait for the swap (default 40)
--
-- Two rules make this test mean anything:
--
--   * Read with readv_u8, never read_u8. read_u8 goes through MAME's normal
--     memory path WITH side effects, so dumping 32K of a MegaCart would fire
--     the bank hotspots and scramble the comparison mid-dump. readv_u8 is the
--     debugger's path and our device honours side_effects_disabled().
--   * Test for the magic having VANISHED first. After the swap every mailbox
--     offset reads whatever the booted game put there, so "did the swap
--     happen" has to be answered before anything else is believed.

local EXPECT  = os.getenv("SOAK_EXPECT")
local TIMEOUT = tonumber(os.getenv("SOAK_TIMEOUT") or "40")

local function verdict(s)
    emu.print_info("soak: " .. s)
    manager.machine:exit()
end

if _G.soak_state == nil then _G.soak_state = { done = false } end
local st = _G.soak_state

_G.soak_sub = emu.add_machine_frame_notifier(function ()
    if st.done then return end

    local sp = manager.machine.devices[":maincpu"].spaces["program"]
    local t = manager.machine.time.seconds

    -- $FC09/$FC0A are 'F','N' while the mailbox is live. A booted game owns
    -- those bytes, so their disappearance IS the swap.
    local swapped = sp:readv_u8(0xFC09) ~= 0x46 or sp:readv_u8(0xFC0A) ~= 0x4E

    if not swapped then
        if t >= TIMEOUT then
            st.done = true
            verdict(string.format("FAIL no swap after %ds (BOOT_STATE=%02X "
                .. "BOOT_ERR=%02X ERR=%02X)", TIMEOUT,
                sp:readv_u8(0xFC06), sp:readv_u8(0xFC08), sp:readv_u8(0xFC02)))
        end
        return
    end

    st.done = true

    local f = io.open(EXPECT, "rb")
    if f == nil then return verdict("FAIL cannot open " .. tostring(EXPECT)) end
    local want = f:read("*a")
    f:close()
    if #want ~= 0x8000 then
        return verdict(string.format("FAIL expected file is %d bytes", #want))
    end

    for off = 0, 0x7FFF do
        local got = sp:readv_u8(0x8000 + off)
        local exp = want:byte(off + 1)
        if got ~= exp then
            return verdict(string.format(
                "FAIL window differs at %04X: got %02X want %02X",
                0x8000 + off, got, exp))
        end
    end
    verdict("PASS")
end)
