-- resettest.lua -- M1's reset-survival proof.
--
-- fujitest shows "SEQ n" after its transaction. A soft reset restarts the Z80
-- but not the cart, so the second run must show n+1: the client derived its
-- sequence from the cart's persisted ACKSEQ, not from a counter the reset
-- zeroed.
local T = dofile(os.getenv("SMS_EMU_DIR") .. "/smstext.lua")
local timeout = tonumber(os.getenv("RESET_TIMEOUT") or "60")

-- MAME re-runs the autoboot script on a soft reset, but the Lua state and the
-- emulated clock carry on: the test's state lives in _G.
if _G.__rt_phase == nil then
  _G.__rt_phase = 0
  _G.__rt_at = 0
  _G.__rt_seq = 0
end

local function seq()
  local y = T.find("SEQ")
  return y and tonumber(T.row(y):match("SEQ%s+(%d+)"))
end

T.every_frame(function()
  local now = T.now()
  if _G.__rt_phase == 0 then
    local s = seq()
    if s then
      print(T.screen())
      print(string.format("resettest: SEQ %d at %.2fs; soft reset", s, now))
      _G.__rt_seq = s
      _G.__rt_phase = 1
      _G.__rt_at = now
      manager.machine:soft_reset()
    elseif T.find("ERR") or T.find("NAK") or T.find("NO FUJINET") then
      _G.__rt_phase = 9
      print(T.screen())
      T.verdict(false, "the first transaction failed")
    elseif now > timeout then
      _G.__rt_phase = 9
      print(T.screen())
      T.verdict(false, "no first transaction")
    end
  elseif _G.__rt_phase == 1 then
    local s = (now > _G.__rt_at + 1) and seq()
    if s and s == _G.__rt_seq + 1 then
      _G.__rt_phase = 9
      print(T.screen())
      T.verdict(true, string.format("SEQ %d -> %d across a soft reset", _G.__rt_seq, s))
    elseif s and s ~= _G.__rt_seq then
      _G.__rt_phase = 9
      print(T.screen())
      T.verdict(false, string.format("SEQ %d -> %d", _G.__rt_seq, s))
    elseif now > _G.__rt_at + timeout then
      _G.__rt_phase = 9
      print(T.screen())
      T.verdict(false, "no second transaction after the reset")
    end
  end
end)
