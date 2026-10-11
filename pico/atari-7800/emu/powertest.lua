-- powertest.lua -- a power cycle goes back through the BIOS and the boot
-- block, and the cart's mailbox state survives it.
--
-- fujitest runs one transaction at power-on; after a power cycle (MAME's
-- soft reset: the 7800 has no reset line, so the cart treats any reset as
-- one) it must run again with the NEXT sequence number, ACKSEQ+1, read
-- from the cart -- never 1 from a counter in RAM.
local T = dofile(os.getenv("A78_EMU_DIR") .. "/a78text.lua")

-- MAME runs the autoboot script again after a reset: the state is global.
if _G.__powertest then return end
_G.__powertest = { phase = 0 }
local S = _G.__powertest

local function shown()
  local y = T.find("ACKSEQ:")
  if not y then return nil end
  return tonumber(T.row(y):match("ACKSEQ: (%x%x)") or "", 16)
end

T.every_frame(function()
  if S.phase == 0 and T.find("SSID") and shown() then
    S.first = shown()
    S.phase = 1
    S.t0 = T.now()
    manager.machine:soft_reset()
  elseif S.phase == 1 and T.now() > S.t0 + 1 then
    S.phase = 2
  elseif S.phase == 2 and T.find("SSID") and shown() then
    local second = shown()
    local want = S.first == 255 and 1 or S.first + 1
    T.verdict(second == want, string.format("ACKSEQ %02X then %02X after the power cycle", S.first, second))
  elseif T.now() > 60 then
    T.verdict(false, "timed out in phase " .. S.phase .. "\n" .. T.screen())
  end
end)
