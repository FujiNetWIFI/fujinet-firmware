-- boottest.lua -- M2 and the Tier C soak: fujiboot pushes an image and hands
-- the console to it. Waits for the cart's swap (the device's fujinet_swaps
-- output), then judges the screen at +10 s and +30 s: alive (not one colour)
-- and changing. Prints one VERDICT line that tools/soak.py parses.
--   BOOT_WAIT  seconds to wait for the swap (default 90)
--   SNAP_A/B   seconds after the swap for the two looks (10, 30)
--   BOOT_SWAPS the swap count that means the game is in (1; 2 for a client
--              that was itself served DIRECT, such as fujibank)
local T = dofile(os.getenv("A52_EMU_DIR") .. "/a52text.lua")
local wait = tonumber(os.getenv("BOOT_WAIT") or "90")
local snap_a = tonumber(os.getenv("SNAP_A") or "10")
local snap_b = tonumber(os.getenv("SNAP_B") or "30")
local swaps_in = tonumber(os.getenv("BOOT_SWAPS") or "1")

_G.bt = _G.bt or { phase = 1, t0 = 0 }
local bt = _G.bt

local function look()
  local scr = manager.machine.screens[":screen"]
  local px = scr:pixels()
  local h, first, varied = 2166136261, px:byte(1), false
  for i = 1, #px, 61 do
    local b = px:byte(i)
    if b ~= first then varied = true end
    h = ((h ~ b) * 16777619) & 0xFFFFFFFF
  end
  scr:snapshot()
  return string.format("%08x", h), varied
end

T.every_frame(function()
  local t = T.now()
  local swaps = manager.machine.output:get_value("fujinet_swaps")
  if bt.phase == 1 then
    if swaps >= swaps_in then
      bt.phase, bt.t0 = 2, t
      print(string.format("boottest: swapped at %.2f s", t))
    elseif t > wait then
      T.verdict(false, "no swap; the client says:\n" .. T.screen())
    end
  elseif bt.phase == 2 and t - bt.t0 >= snap_a then
    bt.ha, bt.va = look()
    bt.phase = 3
  elseif bt.phase == 3 and t - bt.t0 >= snap_b then
    local hb, vb = look()
    local alive = bt.va and vb
    T.verdict(alive, string.format("swap %.2f s; +%ds %s%s; +%ds %s%s; %s",
      bt.t0, snap_a, bt.ha, bt.va and "" or " blank", snap_b, hb, vb and "" or " blank",
      bt.ha ~= hb and "changing" or "static"))
  end
end)
