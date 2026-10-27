-- Reset survival: sample ACKSEQ, soft-reset, run again, and require the next
-- sequence to be one higher. A client using a local counter would replay 01.
--
-- MAME re-runs an autoboot script on soft reset without restarting the clock,
-- so the guard below keeps the original notifier rather than arming another.
if _G._rt_armed then return end
_G._rt_armed = true

local RESET_AT = tonumber(os.getenv("RESET_AT") or "260")
local CHECK_AT = RESET_AT + tonumber(os.getenv("CHECK_AFTER") or "300")
local before, n = nil, 0

local function acksq()
    return manager.machine.devices[":maincpu"].spaces["program"]:readv_u8(0xFC00)
end

_G._rt = emu.add_machine_frame_notifier(function()
    n = n + 1
    if n == RESET_AT then
        before = acksq()
        print(string.format("RESETTEST: before reset ACKSEQ=%02X", before))
        manager.machine:soft_reset()
    elseif n == CHECK_AT then
        local after = acksq()
        print(string.format("RESETTEST: after  reset ACKSEQ=%02X", after))
        if before == 0x01 and after == 0x02 then
            print("RESETTEST: PASS -- the cart kept its sequence across a console reset")
        else
            print(string.format(
                "RESETTEST: FAIL -- expected 01 then 02, got %02X then %02X",
                before or 0xFF, after))
        end
        manager.machine:exit()
    end
end)
