-- waitshot.lua -- wait for SHOT_TEXT on screen, print the screen, save a
-- snapshot and exit; fail after SHOT_TIMEOUT seconds.
local T = dofile(os.getenv("SMS_EMU_DIR") .. "/smstext.lua")
local text = os.getenv("SHOT_TEXT") or ""
local timeout = tonumber(os.getenv("SHOT_TIMEOUT") or "60")
local done = false

T.every_frame(function()
  if done then return end
  if T.find(text) then
    done = true
    print(T.screen())
    manager.machine.screens[":screen"]:snapshot()
    T.verdict(true, "saw " .. text)
  elseif T.now() > timeout then
    done = true
    print(T.screen())
    T.verdict(false, "no " .. text)
  end
end)
