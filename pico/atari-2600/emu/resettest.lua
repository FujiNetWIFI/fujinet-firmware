-- resettest.lua -- M1's real evidence: the cartridge survives a console reset.
--
-- There is no reset line on the 2600 cartridge connector, and the console's
-- RESET switch is a bit in a RIOT register the cart never sees. So a reset
-- restarts the CLIENT -- clearing its RAM and its variables -- while the
-- CARTRIDGE keeps everything, including the last acknowledged sequence number.
--
-- That is why every port in this family derives its next sequence from the
-- cart's own persisted ACKSEQ + 1 rather than from a counter in RAM. A client
-- that restarted its counter would ask for sequence 1 again, fujimail.c would
-- see a sequence it has already answered, and the transaction would be dropped
-- in silence -- the client would simply hang, with nothing on screen to say
-- why.
--
-- PASS requires the post-reset sequence to be exactly one more than the
-- pre-reset one. Seeing 01 twice is the bug this test exists to catch.

local FN_ACKSEQ = 0x1F00
local FN_MAG0   = 0x1F09

local sp, phase, before, waited = nil, "first", nil, 0

-- The script is re-executed on a soft reset in some MAME builds; keep the
-- state in _G so the second run does not start over.
if _G._reset_state == nil then _G._reset_state = {} end
local st = _G._reset_state

_G._resettest = emu.add_machine_frame_notifier(function()
    sp = sp or manager.machine.devices[":maincpu"].spaces["program"]
    if st.done then return end

    local seq = sp:readv_u8(FN_ACKSEQ)
    local magic = sp:readv_u8(FN_MAG0) == 0x46

    waited = waited + 1
    if waited > 900 then
        print(string.format("FAIL: timed out in phase %s (ACKSEQ=%02X, magic=%s)",
                            st.phase or phase, seq, tostring(magic)))
        st.done = true
        manager.machine:exit()
        return
    end

    if st.phase == nil then st.phase = "first" end

    if st.phase == "first" then
        if magic and seq ~= 0 then
            st.before = seq
            print(string.format("before reset: ACKSEQ=%02X", seq))
            st.phase = "reset"
            manager.machine:soft_reset()
            waited = 0
        end
        return
    end

    if st.phase == "reset" then
        -- Wait for the restarted client to complete its own transaction. The
        -- cart's ACKSEQ does not go back to zero across the reset, so the tell
        -- is that it CHANGED, not that it became non-zero.
        if magic and seq ~= st.before then
            local want = st.before + 1
            if want > 255 then want = 1 end
            print(string.format("after reset:  ACKSEQ=%02X (want %02X)", seq, want))
            if seq == want then
                print("PASS: the cartridge kept its sequence across a console reset")
            else
                print("FAIL: the client did not derive its sequence from the cart")
            end
            st.done = true
            manager.machine:exit()
        end
        return
    end
end)
