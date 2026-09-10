-- Reset survival: the cartridge must NOT restart when the console does.
--
-- The client derives every sequence number from the cart's own persisted
-- ACKSEQ + 1, never from a counter in its own RAM, because a console reset
-- re-zeroes the client's variables while the cart keeps running. On this
-- console that is not just convention -- the connector carries no reset line
-- at all, so the cart cannot be reset by the console even in principle.
--
-- So: run, sample ACKSEQ, soft-reset, run again, and require the second
-- sequence to be one HIGHER. A client using a local counter would replay 01
-- and read a stale reply as a fresh one.
--
-- MAME re-executes an autoboot script on soft reset but does NOT restart the
-- emulated clock, so the guard below keeps the original notifier and its frame
-- count rather than arming a second one.
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
