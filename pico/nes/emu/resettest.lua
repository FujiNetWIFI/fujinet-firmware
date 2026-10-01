-- resettest.lua -- M1's reset-survival proof.
--
-- fujitest shows "ACKSEQ: 01" after its transaction. A soft reset restarts
-- the 6502 but not the cart, so the second run must show "ACKSEQ: 02": the
-- client derived its sequence from the cart's persisted ACKSEQ, not from a
-- counter of its own that the reset zeroed.
local T = dofile(os.getenv("NES_EMU_DIR") .. "/nestext.lua")
local timeout = tonumber(os.getenv("RESET_TIMEOUT") or "30")

-- MAME re-executes the autoboot script on a soft reset, but the Lua state and
-- the emulated clock carry on: the test's state has to live in _G, and this
-- second execution must not start a second test.
if _G.__resettest_phase == nil then
  _G.__resettest_phase = 0
  _G.__resettest_reset_at = 0
end

T.every_frame(function()
  local now = T.now()
  local phase = _G.__resettest_phase
  local reset_at = _G.__resettest_reset_at
  if phase == 0 then
    if T.find("ACKSEQ: 01") then
      print(T.screen())
      print(string.format("resettest: first transaction done at %.2fs; soft reset", now))
      _G.__resettest_phase = 1
      _G.__resettest_reset_at = now
      manager.machine:soft_reset()
    elseif T.find("FAILED") then
      _G.__resettest_phase = 9
      print(T.screen())
      T.verdict(false, "the first transaction failed")
    elseif now > timeout then
      _G.__resettest_phase = 9
      print(T.screen())
      T.verdict(false, "no first transaction")
    end
  elseif phase == 1 then
    -- the screen is rebuilt after the reset; wait for the new ACKSEQ row
    if now > reset_at + 0.5 and T.find("ACKSEQ: 02") then
      _G.__resettest_phase = 9
      print(T.screen())
      T.verdict(true, "ACKSEQ 01 -> 02 across a soft reset")
    elseif now > reset_at + 3 and T.find("ACKSEQ: 01") then
      _G.__resettest_phase = 9
      print(T.screen())
      T.verdict(false, "ACKSEQ still 01 after the reset: the client replayed a sequence")
    elseif now > reset_at + timeout then
      _G.__resettest_phase = 9
      print(T.screen())
      T.verdict(false, "no second transaction after the reset")
    end
  end
end)
