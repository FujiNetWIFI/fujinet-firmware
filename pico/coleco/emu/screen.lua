-- screen.lua -- read the ColecoVision's text screen out of VRAM and print it.
--
-- In OS7's Graphics 1 mode the name table at VRAM $1800 holds ASCII codes, so the screen
-- reads back as text.
--
--   SCREEN_AT      emulated seconds to let the client run before sampling
--                  (default 2); measured from the reset when one is requested,
--                  so -seconds_to_run must exceed SCREEN_RESET_AT + SCREEN_AT
--   SCREEN_EXPECT  substring that must appear somewhere on screen; sets the
--                  exit verdict when given
--   SCREEN_QUIET   set to only print the verdict line, not the screen
--   SCREEN_RESET_AT  emulated seconds at which to soft-reset the console first: the
--                  client must come back with a higher sequence number, not replay one
--                  the cart already answered.

local SAMPLE_AT = tonumber(os.getenv("SCREEN_AT") or "2")   -- emulated seconds
local EXPECT  = os.getenv("SCREEN_EXPECT")
local QUIET   = os.getenv("SCREEN_QUIET")

local PNT     = 0x1800
local COLS    = 32
local ROWS    = 24

local RESET_AT = tonumber(os.getenv("SCREEN_RESET_AT") or "0")

-- MAME reruns this script on a soft reset, so the state lives in _G. The emulated clock does
-- not restart, so the sample after a reset is at RESET_AT + SAMPLE_AT.
if _G.screen_state == nil then
    _G.screen_state = { fired = false, reset_done = false }
end
local run = _G.screen_state

local function read_screen()
    local vdp = manager.machine.devices[":tms9928a"]
    if vdp == nil then
        return nil, "no :tms9928a device"
    end
    -- The VDP owns its 16K of VRAM in its own address space.
    local vram = vdp.spaces["vram"] or vdp.spaces["videoram"] or vdp.spaces["data"]
    if vram == nil then
        local names = {}
        for key in pairs(vdp.spaces) do names[#names + 1] = key end
        return nil, "no VRAM space (have: " .. table.concat(names, ",") .. ")"
    end

    -- A selected row is drawn from the inverse charset at +0x60 (see
    -- disp_row_invert), so decode that range back to readable text or the dump
    -- shows a blank line exactly where the cursor is. Inverse rows are marked
    -- so a verdict can assert WHERE the bar is, not just what the text says.
    local lines = {}
    local marks = {}
    for row = 0, ROWS - 1 do
        local chars = {}
        local inverse = false
        for col = 0, COLS - 1 do
            local byte = vram:read_u8(PNT + row * COLS + col)
            if byte >= 0x80 and byte <= 0xDF then
                byte = byte - 0x60
                inverse = true
            end
            if byte < 32 or byte > 126 then byte = 32 end
            chars[#chars + 1] = string.char(byte)
        end
        lines[#lines + 1] = table.concat(chars)
        marks[#marks + 1] = inverse
    end
    return lines, nil, marks
end

-- The subscription MUST be kept in a live global. add_machine_frame_notifier
-- returns an RAII token, and dropping it unsubscribes at the next Lua garbage
-- collection -- which is why a callback scheduled a couple of seconds out fires
-- and one scheduled ten seconds out silently never does.
_G.screen_sub = emu.add_machine_frame_notifier(function ()
    if RESET_AT > 0 and not run.reset_done
       and manager.machine.time.seconds >= RESET_AT then
        run.reset_done = true
        run.ackseq_before = manager.machine.devices[":maincpu"]
            .spaces["program"]:readv_u8(0xFC00)
        emu.print_info(string.format("screen: soft reset (ACKSEQ was %d)",
                                     run.ackseq_before))
        manager.machine:soft_reset()
        return
    end
    local due = (RESET_AT > 0) and (RESET_AT + SAMPLE_AT) or SAMPLE_AT

    if run.fired or manager.machine.time.seconds < due then return end
    run.fired = true

    local lines, err, marks = read_screen()
    if lines == nil then
        emu.print_info("screen: FAIL " .. err)
        manager.machine:exit()
        return
    end

    if not QUIET then
        emu.print_info("+--------------------------------+")
        for i, l in ipairs(lines) do
            -- '#' marks the selection bar, '|' an ordinary row.
            local edge = marks[i] and "#" or "|"
            emu.print_info(edge .. l .. edge)
        end
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

    if run.reset_done then
        local now = manager.machine.devices[":maincpu"]
            .spaces["program"]:readv_u8(0xFC00)

        -- The whole point of the reset test. A client that derived its
        -- sequence number locally would reissue one the cartridge had already
        -- acknowledged, get silence, and go on displaying the stale reply.
        if now ~= run.ackseq_before then
            emu.print_info(string.format(
                "screen: reset-survival PASS (ACKSEQ %d -> %d)",
                run.ackseq_before, now))
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
