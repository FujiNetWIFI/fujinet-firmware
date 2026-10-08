-- cfgtest.lua -- drive the real CONFIG with the joypad: splash, hosts, the
-- SD host, CFG_DIR, CFG_FILE, boot; pass once the cart has flipped and the
-- game has run CFG_RUN seconds.
local T = dofile(os.getenv("SMS_EMU_DIR") .. "/smstext.lua")
local host = os.getenv("CFG_HOST") or "SD"
local dir = os.getenv("CFG_DIR") or "sms/"
local file = os.getenv("CFG_FILE") or "alexkidd.bin"
local run = tonumber(os.getenv("CFG_RUN") or "10")
local limit = tonumber(os.getenv("CFG_LIMIT") or "150")
local pad = manager.machine.ioport.ports[":ctrl1:mspad:JOYPAD"].fields
local out = manager.machine.output

local step, hold, wait, flipped = "splash", nil, 0, nil

local function press(name, frames)
  hold = { field = pad[name], left = frames or 6 }
  hold.field:set_value(1)
end

-- Move the bar onto the row showing `text`; true once it is there.
local function bar_to(text)
  local y = T.find(text)
  if not y then return false end
  for r = 1, 17 do
    if T.hi(r) then
      if r == y then return true end
      press(r < y and "P1 Down" or "P1 Up", 4)
      return false
    end
  end
  return false
end

T.every_frame(function()
  local now = T.now()
  if hold then
    hold.left = hold.left - 1
    if hold.left == 0 then hold.field:set_value(0); hold = nil; wait = 12 end
    return
  end
  if wait > 0 then wait = wait - 1; return end
  if now > limit then
    print(T.screen()); T.verdict(false, "stuck at " .. step); return
  end
  if step == "splash" then
    if T.find("PRESS BUTTON 1") then press("P1 Button 1"); step = "hosts" end
  elseif step == "hosts" then
    if T.find("SELECT A HOST") and bar_to(host) then
      print(T.screen()); press("P1 Button 1"); step = "dir"
    end
  elseif step == "dir" then
    if bar_to(dir) then print(T.screen()); press("P1 Button 1"); step = "file" end
  elseif step == "file" then
    if T.find(file) and bar_to(file) then
      print(T.screen()); press("P1 Button 1"); step = "boot"
    end
  elseif step == "boot" then
    if out:get_value("fujinet_mode") ~= 0 then flipped = now; step = "run" end
    if T.find("EMOUNT") or T.find("ELOAD") or T.find("EPATH") then
      print(T.screen()); T.verdict(false, "boot failed")
    end
  elseif step == "run" then
    if now > flipped + run then
      manager.machine.screens[":screen"]:snapshot()
      T.verdict(true, string.format("booted %s, flipped at %.1fs", file, flipped))
    end
  end
end)
