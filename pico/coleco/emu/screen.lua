-- screen.lua -- read the ColecoVision's text screen out of VRAM and print it.
--
-- The verdict harness for every headless run. OS7's Graphics-1 mode puts the
-- pattern name table at VRAM $1800 (32x24 bytes of character codes) and
-- load_ascii fills the generator with the BIOS's own ASCII set, so a name-table
-- byte IS the character -- no glyph decoding needed. That makes "what is on
-- screen" a string comparison rather than a pixel diff, which is what lets
-- drive.lua and soak.lua give one-line verdicts.
--
--   SCREEN_AT      emulated seconds to let the client run before sampling
--                  (default 2); measured from the reset when one is requested,
--                  so -seconds_to_run must exceed SCREEN_RESET_AT + SCREEN_AT
--   SCREEN_EXPECT  substring that must appear somewhere on screen; sets the
--                  exit verdict when given
--   SCREEN_QUIET   set to only print the verdict line, not the screen
--   SCREEN_RESET_AT  emulated seconds at which to soft-reset the console
--                  first. This is the reset-survival check: the client must
--                  come back with a HIGHER sequence number and a fresh
--                  transaction, not replay the reply it already has. A console
--                  RESET restarts the program but not the cartridge, so a
--                  client that counted sequence numbers locally would reissue
--                  one the cartridge had already acknowledged and be answered
--                  with silence -- while the stale reply on screen went on
--                  looking like success. That is exactly how the Intellivision
--                  bring-up fooled itself for a session.

local AT      = tonumber(os.getenv("SCREEN_AT") or "2")   -- emulated seconds
local EXPECT  = os.getenv("SCREEN_EXPECT")
local QUIET   = os.getenv("SCREEN_QUIET")

local PNT     = 0x1800
local COLS    = 32
local ROWS    = 24

local RESET_AT = tonumber(os.getenv("SCREEN_RESET_AT") or "0")

-- MAME re-executes the autoboot script on every soft reset, so the "have I
-- already reset?" flag has to outlive the script: keep it in _G, or the
-- harness resets forever. The emulated clock, on the other hand, keeps running
-- across a soft reset -- which is why the sample time below is RESET_AT + AT
-- and not AT. Sampling at AT would fire one frame after the reset, catch the
-- client before it has re-run, and read VRAM that is still showing the
-- PREVIOUS run's screen: a convincing pass on content and a spurious failure
-- on everything else.
if _G.screen_state == nil then
    _G.screen_state = { fired = false, reset_done = false }
end
local st = _G.screen_state

local function read_screen()
    local vdp = manager.machine.devices[":tms9928a"]
    if vdp == nil then
        return nil, "no :tms9928a device"
    end
    -- The VDP owns its 16K of VRAM in its own address space.
    local vram = vdp.spaces["vram"] or vdp.spaces["videoram"] or vdp.spaces["data"]
    if vram == nil then
        local names = {}
        for k in pairs(vdp.spaces) do names[#names + 1] = k end
        return nil, "no VRAM space (have: " .. table.concat(names, ",") .. ")"
    end

    local lines = {}
    for row = 0, ROWS - 1 do
        local chars = {}
        for col = 0, COLS - 1 do
            local b = vram:read_u8(PNT + row * COLS + col)
            if b < 32 or b > 126 then b = 32 end
            chars[#chars + 1] = string.char(b)
        end
        lines[#lines + 1] = table.concat(chars)
    end
    return lines
end

-- The subscription MUST be kept in a live global. add_machine_frame_notifier
-- returns an RAII token, and dropping it unsubscribes at the next Lua garbage
-- collection -- which is why a callback scheduled a couple of seconds out fires
-- and one scheduled ten seconds out silently never does.
_G.screen_sub = emu.add_machine_frame_notifier(function ()
    if RESET_AT > 0 and not st.reset_done
       and manager.machine.time.seconds >= RESET_AT then
        st.reset_done = true
        st.ackseq_before = manager.machine.devices[":maincpu"]
            .spaces["program"]:readv_u8(0xFC00)
        emu.print_info(string.format("screen: soft reset (ACKSEQ was %d)",
                                     st.ackseq_before))
        manager.machine:soft_reset()
        return
    end
    local due = (RESET_AT > 0) and (RESET_AT + AT) or AT

    if st.fired or manager.machine.time.seconds < due then return end
    st.fired = true

    local lines, err = read_screen()
    if lines == nil then
        emu.print_info("screen: FAIL " .. err)
        manager.machine:exit()
        return
    end

    if not QUIET then
        emu.print_info("+--------------------------------+")
        for _, l in ipairs(lines) do emu.print_info("|" .. l .. "|") end
        emu.print_info("+--------------------------------+")
    end

    -- The Z80's PC, because "the screen looks right" is not proof: after a
    -- soft reset VRAM keeps whatever was there, so a client that hangs before
    -- it redraws shows a perfectly convincing stale screen.
    do
        local cpu = manager.machine.devices[":maincpu"]
        emu.print_info(string.format("screen: PC=%04X ACKSEQ=%02X ERR=%02X",
            cpu.state["PC"].value,
            cpu.spaces["program"]:readv_u8(0xFC00),
            cpu.spaces["program"]:readv_u8(0xFC02)))
    end

    if st.reset_done then
        local now = manager.machine.devices[":maincpu"]
            .spaces["program"]:readv_u8(0xFC00)

        -- The whole point of the reset test. A client that derived its
        -- sequence number locally would reissue one the cartridge had already
        -- acknowledged, get silence, and go on displaying the stale reply.
        if now ~= st.ackseq_before then
            emu.print_info(string.format(
                "screen: reset-survival PASS (ACKSEQ %d -> %d)",
                st.ackseq_before, now))
        else
            emu.print_info(string.format(
                "screen: reset-survival FAIL (ACKSEQ stuck at %d: the client "
                .. "replayed instead of asking again)", now))
        end
    end

    if EXPECT then
        local joined = table.concat(lines, "\n")
        if joined:find(EXPECT, 1, true) then
            emu.print_info("screen: PASS")
        else
            emu.print_info("screen: FAIL expected " .. string.format("%q", EXPECT))
        end
    end
    manager.machine:exit()
end)
