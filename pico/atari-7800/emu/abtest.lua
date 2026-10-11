-- abtest.lua -- the same input script and the same snapshots on both sides of
-- an A/B run. Everything is keyed on the frame count, which an identical
-- machine reaches identically. A snapshot every AB_EVERY frames, AB_SHOTS of
-- them; then exit.
local every = tonumber(os.getenv("AB_EVERY") or "120")
local shots = tonumber(os.getenv("AB_SHOTS") or "30")
local frame, taken = 0, 0
local ports = manager.machine.ioport.ports

local function set(port, name, on)
  local p = ports[port]
  local f = p and p.fields[name]
  if f then f:set_value(on and 1 or 0) end
end

_G.__ab_sub = emu.add_machine_frame_notifier(function()
  frame = frame + 1
  -- From 5 s: tap fire every 2 s, the second button every 4 s, Reset once
  -- at 6 s (some games start from the console switch), and lean right for
  -- one second in four, so title screens give way to play.
  local f = frame - 300
  set(":BUTTONS", "P1 Button 1", f >= 0 and f % 120 < 6)
  set(":BUTTONS", "P1 Button 2", f >= 0 and f % 240 >= 60 and f % 240 < 66)
  set(":CONSOLE", "Reset", f >= 60 and f < 66)
  set(":JOYSTICKS", "P1 Right", f >= 0 and f % 240 >= 120 and f % 240 < 180)
  if frame % every == 0 then
    manager.machine.screens[":screen"]:snapshot()
    taken = taken + 1
    if taken >= shots then manager.machine:exit() end
  end
end)
