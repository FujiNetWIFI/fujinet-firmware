-- drive.lua -- one real transaction, end to end to the raster.
-- Builds the screen the live reply should produce and writes it for
-- emu/dispcheck.py to compare with the snapshot. The expectation comes from the
-- reply, never from the planes; every wait is on program state, not frames.

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

    -- Mailbox reads have no side effects; readv_u8 still runs MAME's handlers.
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
