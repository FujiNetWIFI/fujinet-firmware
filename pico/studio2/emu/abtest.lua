-- abtest.lua -- Tier A: one side of a frame-equality run. The same keypad
-- script on both sides (stock MAME's cart, and the FujiNet device serving
-- the same image DIRECT), a snapshot every 2 s for AB_SECS (default 60);
-- tools/abrun.py compares the two directories. Reads nothing through the
-- address space, so it is the same script whether the device exists or not.
local secs = tonumber(os.getenv("AB_SECS") or "60")
local script = {            -- {second, pad, key}: start a game, then play a bit
  { 3, "A", 1 }, { 5, "A", 2 }, { 7, "B", 1 }, { 9, "A", 5 }, { 11, "B", 5 },
  { 13, "A", 4 }, { 15, "A", 6 }, { 17, "B", 8 }, { 19, "A", 0 }, { 21, "B", 2 },
  { 25, "A", 8 }, { 29, "B", 4 }, { 33, "A", 3 }, { 37, "B", 6 }, { 41, "A", 9 },
  { 45, "B", 0 }, { 49, "A", 7 }, { 53, "B", 9 }, { 57, "A", 5 },
}
_G.ab = _G.ab or { frame = 0, held = nil, step = 1 }
local ab = _G.ab

_G.__ab_sub = emu.add_machine_frame_notifier(function()
  ab.frame = ab.frame + 1
  local t = ab.frame / 60
  if ab.held then
    ab.held.left = ab.held.left - 1
    if ab.held.left <= 0 then ab.held.field:clear_value(); ab.held = nil end
  end
  local s = script[ab.step]
  if s and t >= s[1] then
    local f = manager.machine.ioport.ports[":" .. s[2]].fields[s[2] .. " " .. s[3]]
    f:set_value(1)
    ab.held = { field = f, left = 8 }
    ab.step = ab.step + 1
  end
  if ab.frame % 120 == 0 then manager.machine.video:snapshot() end
  if t >= secs then manager.machine:exit() end
end)
