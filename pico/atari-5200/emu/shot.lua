-- shot.lua -- print the screen as text and save a snapshot after SHOT_AT
-- seconds, then exit.
local T = dofile(os.getenv("A52_EMU_DIR") .. "/a52text.lua")
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
