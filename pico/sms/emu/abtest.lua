-- abtest.lua -- the same input script and the same snapshots on both sides of
-- an A/B run. Everything is keyed on the frame count, which an identical
-- machine reaches identically. A snapshot every AB_EVERY frames, AB_SHOTS of
-- them; then exit.
local every = tonumber(os.getenv("AB_EVERY") or "120")
local shots = tonumber(os.getenv("AB_SHOTS") or "30")
local frame, taken = 0, 0
local pad = manager.machine.ioport.ports[":ctrl1:mspad:JOYPAD"]
local fields = pad and pad.fields or {}

local function set(name, on)
  local f = fields[name]
  if f then f:set_value(on and 1 or 0) end
end

_G.__ab_sub = emu.add_machine_frame_notifier(function()
  frame = frame + 1
  -- From 5 s: tap button 1 every 2 s, button 2 every 4 s, and lean right
  -- for one second in four, so title screens give way to play.
  local f = frame - 300
  set("P1 Button 1", f >= 0 and f % 120 < 6)
  set("P1 Button 2", f >= 0 and f % 240 >= 60 and f % 240 < 66)
  set("P1 Right", f >= 0 and f % 240 >= 120 and f % 240 < 180)
  if frame % every == 0 then
    manager.machine.screens[":screen"]:snapshot()
    taken = taken + 1
    if taken >= shots then manager.machine:exit() end
  end
end)
