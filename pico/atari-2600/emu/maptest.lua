-- maptest.lua -- M5 end to end: a booted GAME is served by its real board.
--
-- Boots an 8K F8 image over the network and then drives the bankswitch
-- hotspots from the emulator side, requiring the served window to follow --
-- and requiring it to follow with MAME's ordering, where the hotspot access
-- returns the byte from the bank that was live BEFORE it.
--
-- host_test/test_vcsmap.c is the real conformance check (nine schemes against
-- verbatim transcriptions of MAME's handlers, fuzzed). This is the seam that
-- test cannot reach: that a pushed image actually arrives at the mapper, that
-- the claim is what routes it there, and that the debugger's own reads do not
-- move the bank while a harness is dumping the window.

local FN_CLAIM = 0x1F10
local WIN, BASE = 0x1000, 0x1000

local want_file = os.getenv("BOOT_IMAGE")
local sp, done, waited = nil, false, 0

local function claimed()
    return sp:readv_u8(FN_CLAIM) == 0x46 and sp:readv_u8(FN_CLAIM + 1) == 0x55
end

-- Compare the served window against one 4K bank of the file.
local function match_bank(img, bank)
    local base = bank * 0x1000
    local bad, first = 0, nil
    for i = 0, WIN - 1 do
        local got = sp:readv_u8(BASE + i)
        local exp = img:byte(base + i + 1)
        if got ~= exp then
            bad = bad + 1
            if not first then
                first = string.format("$%04X: want $%02X got $%02X",
                                      BASE + i, exp, got)
            end
        end
    end
    return bad, first
end

_G._maptest = emu.add_machine_frame_notifier(function()
    if done then return end
    sp = sp or manager.machine.devices[":maincpu"].spaces["program"]

    if claimed() then
        waited = waited + 1
        if waited > 1800 then
            print("FAIL: the swap never happened")
            done = true
            manager.machine:exit()
        end
        return
    end
    done = true
    print("swap detected: the claim is gone, the image is a game")

    local f = io.open(want_file, "rb")
    if not f then
        print("FAIL: cannot open " .. tostring(want_file))
        manager.machine:exit()
        return
    end
    local img = f:read("*a")
    f:close()
    if #img ~= 8192 then
        print(string.format("FAIL: %s is %d bytes, want 8192", want_file, #img))
        manager.machine:exit()
        return
    end

    -- The whole dump above used readv_u8, which MAME runs with side effects
    -- disabled. If the device were missing that guard, those 4096 reads would
    -- have walked straight over the bank hotspots at $1FF8/$1FF9 and this
    -- comparison would be against a mixture of banks.
    local bad, first = match_bank(img, 0)
    if bad > 0 then
        print(string.format("FAIL: bank 0 mismatch, %d bytes. First at %s",
                            bad, first))
        manager.machine:exit()
        return
    end
    print("bank 0 served correctly, and 4096 debugger reads did not move it")

    -- Now switch, with side effects: a write to the hotspot, which real
    -- hardware cannot tell from a read.
    sp:write_u8(0x1FF9, 0)
    bad, first = match_bank(img, 1)
    if bad > 0 then
        print(string.format("FAIL: after the hotspot, bank 1 mismatch, "
                            .. "%d bytes. First at %s", bad, first))
        manager.machine:exit()
        return
    end
    print("hotspot $1FF9 switched to bank 1")

    sp:write_u8(0x1FF8, 0)
    bad, first = match_bank(img, 0)
    if bad > 0 then
        print(string.format("FAIL: after $1FF8, bank 0 mismatch, %d bytes. "
                            .. "First at %s", bad, first))
        manager.machine:exit()
        return
    end

    print("PASS: an 8K F8 game booted over the network and banks correctly, "
          .. "both ways, with the debugger read path inert")
    manager.machine:exit()
end)
