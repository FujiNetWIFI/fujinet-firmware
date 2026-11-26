-- resettest.lua -- the cartridge keeps its state across a console reset.
-- The edge has no reset line, so RESET restarts only the client, which must
-- take its next sequence from the cart's ACKSEQ + 1: a restarted counter would
-- repeat a sequence fujimail.c has answered and hang. PASS: ACKSEQ up by one.

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
        -- ACKSEQ survives the reset, so wait for it to change, not to be non-zero.
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
