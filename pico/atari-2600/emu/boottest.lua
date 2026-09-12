-- boottest.lua -- M2: the pushed image is byte-identical, and it runs.
--
-- Waits for the swap, then dumps the WHOLE served 4K window through the
-- debugger and compares it against the file on disk. Not a checksum and not a
-- spot check: every byte, because a DBC push that truncates or an off-by-one
-- in the staging buffer produces an image that boots and then misbehaves
-- somewhere else entirely.
--
-- The tell that the swap happened is the claim vanishing from $1F10. The boot
-- target has its "FUJI" deliberately stripped, so it is a GAME as far as the
-- cartridge is concerned -- which also proves the mailbox goes dead for an
-- image that does not claim, the way it must for a real cartridge.
--
-- Waits on that state, never on a frame number, so a slow socket round trip
-- cannot turn into a flake.

local WIN, BASE = 0x1000, 0x1000
local FN_CLAIM  = 0x1F10
local FN_BST    = 0x1F06
local FN_ERR    = 0x1F02

local want = os.getenv("BOOT_IMAGE")
local sp, done, waited = nil, false, 0

local function claimed()
    return sp:readv_u8(FN_CLAIM)     == 0x46   -- 'F'
       and sp:readv_u8(FN_CLAIM + 1) == 0x55   -- 'U'
       and sp:readv_u8(FN_CLAIM + 2) == 0x4A   -- 'J'
       and sp:readv_u8(FN_CLAIM + 3) == 0x49   -- 'I'
end

_G._boottest = emu.add_machine_frame_notifier(function()
    if done then return end
    sp = sp or manager.machine.devices[":maincpu"].spaces["program"]

    if claimed() then
        -- Still the client. Surface a boot failure rather than timing out
        -- silently: the client parks its own error on screen, but BOOT_ERR is
        -- the cartridge's side of the story.
        waited = waited + 1
        if waited % 300 == 0 then
            print(string.format("waiting... BOOT_STATE=%02X ERR=%02X",
                                sp:readv_u8(FN_BST), sp:readv_u8(FN_ERR)))
        end
        if waited > 1800 then
            print("FAIL: the swap never happened")
            done = true
            manager.machine:exit()
        end
        return
    end
    done = true
    print("swap detected: the claim is gone from $1F10")

    local f = io.open(want, "rb")
    if not f then
        print("FAIL: cannot open " .. tostring(want))
        manager.machine:exit()
        return
    end
    local img = f:read("*a")
    f:close()

    if #img ~= WIN then
        print(string.format("FAIL: %s is %d bytes, want %d", want, #img, WIN))
        manager.machine:exit()
        return
    end

    local bad, first = 0, nil
    for i = 0, WIN - 1 do
        local got = sp:readv_u8(BASE + i)
        local exp = img:byte(i + 1)
        -- The two write-only pages are never driven, so the debugger reads
        -- open bus there rather than image bytes. They are excluded, and they
        -- are the ONLY exclusion.
        local page = (BASE + i) & 0xFF00
        if page ~= 0x1D00 and page ~= 0x1E00 and got ~= exp then
            bad = bad + 1
            if not first then
                first = string.format("$%04X: want $%02X got $%02X",
                                      BASE + i, exp, got)
            end
        end
    end

    if bad > 0 then
        print(string.format("FAIL: %d of %d served bytes differ. First at %s",
                            bad, WIN, first))
    else
        print(string.format("PASS: all %d served bytes match %s, "
                            .. "and the booted image is running", WIN, want))
    end
    manager.machine:exit()
end)
