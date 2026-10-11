-- soak.lua -- Tier C: fujiboot pushes, loads and starts an image; once the
-- cart has flipped, play the A/B input script and take snapshots at
-- +SOAK_AT1 and +SOAK_AT2 seconds, then exit.
local at1 = tonumber(os.getenv("SOAK_AT1") or "10")
local at2 = tonumber(os.getenv("SOAK_AT2") or "30")
local limit = tonumber(os.getenv("SOAK_LIMIT") or "120")
local every = tonumber(os.getenv("SOAK_EVERY") or "0")   -- 0: just at1 and at2
local pad = manager.machine.ioport.ports[":ctrl1:mspad:JOYPAD"]
local fields = pad and pad.fields or {}
local flipped, shots, frame = nil, 0, 0

local function set(name, on)
  local f = fields[name]
  if f then f:set_value(on and 1 or 0) end
end

-- The device publishes its mode as the "fujinet_mode" output.
local T = dofile(os.getenv("SMS_EMU_DIR") .. "/smstext.lua")
local out = manager.machine.output

_G.__soak_sub = emu.add_machine_frame_notifier(function()
  local now = T.now()
  frame = frame + 1
  if not flipped then
    if out:get_value("fujinet_mode") ~= 0 then
      flipped = now; frame = 0; print(string.format("SOAK flipped %.2f", now))
    end
    if T.find("ELOAD") or T.find("EMOUNT") or T.find("EHOST") or T.find("EPATH") then
      print(T.screen()); print("SOAK FAIL boot"); manager.machine:exit()
    end
    if now > limit then print(T.screen()); print("SOAK FAIL timeout"); manager.machine:exit() end
    return
  end
  local f = frame - 300
  set("P1 Button 1", f >= 0 and f % 120 < 6)
  set("P1 Button 2", f >= 0 and f % 240 >= 60 and f % 240 < 66)
  set("P1 Right", f >= 0 and f % 240 >= 120 and f % 240 < 180)
  if every > 0 then
    if now >= flipped + every * (shots + 1) then
      manager.machine.screens[":screen"]:snapshot()
      shots = shots + 1
      if now >= flipped + at2 then print("SOAK DONE"); manager.machine:exit() end
    end
  elseif (shots == 0 and now >= flipped + at1) or (shots == 1 and now >= flipped + at2) then
    manager.machine.screens[":screen"]:snapshot()
    shots = shots + 1
    if shots == 2 then print("SOAK DONE"); manager.machine:exit() end
  end
end)
