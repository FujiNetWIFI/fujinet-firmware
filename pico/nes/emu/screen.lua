-- screen.lua -- wait for text to appear on screen, then PASS; dump the
-- screen and FAIL on timeout.
--   SCREEN_EXPECT   substring that must appear (default "FUJINET")
--   SCREEN_TIMEOUT  seconds (default 20)
--   SCREEN_DUMP     if set, print the whole screen at the verdict
local T = dofile(os.getenv("NES_EMU_DIR") .. "/nestext.lua")
local expect = os.getenv("SCREEN_EXPECT") or "FUJINET"
local timeout = tonumber(os.getenv("SCREEN_TIMEOUT") or "20")
local dump = os.getenv("SCREEN_DUMP")
local done = false

T.every_frame(function()
  if done then return end
  local r = T.find(expect)
  if r then
    done = true
    if dump then print(T.screen()) end
    T.verdict(true, string.format("%q on row %d at %.2fs", expect, r, T.now()))
  elseif T.now() > timeout then
    done = true
    print(T.screen())
    T.verdict(false, string.format("%q not on screen after %ds", expect, timeout))
  end
end)
