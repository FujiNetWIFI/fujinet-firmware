-- drive.lua -- M1: prove one real transaction reached the screen.
--
-- Reads the LIVE reply out of the cartridge's reply window, builds the screen
-- that reply should have produced, and writes it where emu/dispcheck.py can
-- compare it against the raster. So the check runs end to end -- socket,
-- fujimail.c, the reply window, the cartridge's glyph compositor, the text
-- planes, the kernel, the beam -- rather than asserting on any one layer.
--
-- The expectation is built from the reply, never from the planes: reading the
-- planes to decide what the planes should contain would prove only that they
-- agree with themselves.
--
-- Waits on the program's own state (the magic bytes and the published
-- sequence), never on a frame number, so a slow socket round trip cannot turn
-- into a flake.

local FN_RPLY   = 0x1B00
local FN_ACKSEQ = 0x1F00
local FN_ERR    = 0x1F02
local FN_RCMD   = 0x1F03
local FN_RXLO   = 0x1F04
local FN_RXHI   = 0x1F05
local FN_MAG0   = 0x1F09
local FN_MAG1   = 0x1F0A

-- Field offsets in AdapterConfigExtended, from fujiDevice.h.
local AC_SSID, AC_VER, AC_SIP = 0, 125, 140
local COLS = 12

local out = os.getenv("DRIVE_EXPECT") or "expect.txt"
local sp, done, waited = nil, false, 0

local function str(base, off, max)
    local s = ""
    for i = 0, max - 1 do
        local c = sp:readv_u8(base + off + i)
        if c == 0 then break end
        if c < 0x20 or c > 0x7E then c = 0x3F end
        s = s .. string.char(c)
    end
    return s
end

_G._drive = emu.add_machine_frame_notifier(function()
    if done then return end
    sp = sp or manager.machine.devices[":maincpu"].spaces["program"]

    -- readv_u8, not read_u8: the debug read must not fire any handler. On this
    -- cartridge no read has a side effect, but a booted game's mapper does,
    -- and the habit is what keeps a harness honest.
    if sp:readv_u8(FN_MAG0) ~= 0x46 or sp:readv_u8(FN_MAG1) ~= 0x4E then
        waited = waited + 1
        if waited > 600 then
            print("FAIL: no FujiNet cartridge answered (no 'FN' magic)")
            done = true
            manager.machine:exit()
        end
        return
    end

    local seq = sp:readv_u8(FN_ACKSEQ)
    if seq == 0 then
        waited = waited + 1
        if waited > 600 then
            print("FAIL: the client never completed a transaction")
            done = true
            manager.machine:exit()
        end
        return
    end
    done = true

    local err  = sp:readv_u8(FN_ERR)
    local rcmd = sp:readv_u8(FN_RCMD)
    local rxlen = sp:readv_u8(FN_RXLO) + sp:readv_u8(FN_RXHI) * 256

    print(string.format("ACKSEQ=%02X err=%d reply=%02X rxlen=%d",
                        seq, err, rcmd, rxlen))

    if err ~= 0 then
        print(string.format("FAIL: transaction error %d", err))
        manager.machine:exit()
        return
    end
    if rcmd ~= 0x06 then
        print(string.format("FAIL: server replied %02X, not ACK", rcmd))
        manager.machine:exit()
        return
    end
    if rxlen ~= 240 then
        print(string.format("FAIL: reply is %d bytes, want 240", rxlen))
        manager.machine:exit()
        return
    end

    local ssid = str(FN_RPLY, AC_SSID, COLS)
    local ip   = str(FN_RPLY, AC_SIP,  COLS)
    local ver  = str(FN_RPLY, AC_VER,  COLS)
    print(string.format("live: ssid=%q ip=%q version=%q", ssid, ip, ver))

    -- The screen fujitest.asm should have drawn from exactly this reply.
    local rows = {
        [0] = "FUJINET 2600",
        [2] = "SSID", [3] = ssid,
        [5] = "IP",   [6] = ip,
        [8] = "VERSION", [9] = ver,
        [11] = "ACKSEQ", [12] = string.format("%02X", seq),
    }
    local f = io.open(out, "w")
    for r = 0, 20 do f:write((rows[r] or "") .. "\n") end
    f:close()
    print("wrote expectation to " .. out)

    manager.machine:exit()
end)
