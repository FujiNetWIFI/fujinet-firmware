-- shot.lua -- print the screen as text (and save a snapshot) after SHOT_AT
-- seconds, then exit. For looking at a client without a display attached.
local T = dofile(os.getenv("NES_EMU_DIR") .. "/nestext.lua")
local at = tonumber(os.getenv("SHOT_AT") or "5")
local done = false

T.every_frame(function()
  if done then return end
  if T.now() >= at then
    done = true
    print(T.screen())
    local scr = manager.machine.screens[":screen"]
    if scr then scr:snapshot() end
    manager.machine:exit()
  end
end)
