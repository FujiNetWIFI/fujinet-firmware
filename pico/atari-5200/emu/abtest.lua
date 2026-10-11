-- abtest.lua -- the same input script and the same snapshots on both sides of
-- an A/B run. Everything is keyed on the frame count, which an identical
-- machine reaches identically. A snapshot every AB_EVERY frames, AB_SHOTS of
-- them; then exit.
local every = tonumber(os.getenv("AB_EVERY") or "120")
local shots = tonumber(os.getenv("AB_SHOTS") or "30")
local frame, taken = 0, 0
local ports = manager.machine.ioport.ports

local function field(port, name)
  local p = ports[port]
  return p and p.fields[name]
end

local function set(port, name, on)
  local f = field(port, name)
  if f then f:set_value(on and 1 or 0) end
end

local stick = field(":analog_0", "AD Stick X")

_G.__ab_sub = emu.add_machine_frame_notifier(function()
  frame = frame + 1
  -- From 5 s: START once at 6 s (most titles wait for it), the top fire
  -- button every 2 s, the lower one every 4 s, and the stick right for one
  -- second in four, so title screens give way to play.
  local f = frame - 300
  set(":keypad.3", "Start", f >= 60 and f < 66)
  set(":djoy_b", "P1 Button 1", f >= 0 and f % 120 < 6)
  set(":djoy_b", "P1 Button 2", f >= 0 and f % 240 >= 60 and f % 240 < 66)
  if stick then
    if f >= 0 and f % 240 >= 120 and f % 240 < 180 then
      stick:set_value(0xE4)
    else
      stick:clear_value()
    end
  end
  if frame % every == 0 then
    manager.machine.screens[":screen"]:snapshot()
    taken = taken + 1
    if taken >= shots then manager.machine:exit() end
  end
end)
