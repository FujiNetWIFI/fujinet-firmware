-- boottest.lua -- fujiboot pushes an image, the loader copies it, and the
-- game runs: the cart reports the mode switch, and the screen is not blank.
--
-- Prints the cart's mode and hand-over, and two screen hashes 10 s apart;
-- snapshots at both. BOOT_EXPECT=bios|direct checks the hand-over taken.
local T = dofile(os.getenv("A78_EMU_DIR") .. "/a78text.lua")
if _G.__boottest then return end
_G.__boottest = { phase = 0, loads = 0, prev = 0 }
local S = _G.__boottest
local expect = os.getenv("BOOT_EXPECT")
local function out(name) return manager.machine.devices[":"]:output(name):get() end
local scr = manager.machine.screens[":screen"]

-- every pixel the colour of the first
local function blank()
  local px = scr:pixels()
  return px == px:sub(1, 4):rep(#px // 4)
end

local function hash()
  local px = scr:pixels()
  local h = 5381
  for i = 1, #px, 97 do h = (h * 33 + px:byte(i)) % 4294967296 end
  return h
end

T.every_frame(function()
  local mode = out("fujinet_mode")
  if S.phase == 0 then
    -- the first load is fujiboot itself; the second is the pushed image
    if S.prev == 1 and (mode == 2 or mode == 3) then S.loads = S.loads + 1 end
    S.prev = mode
    if S.loads == 2 then
      S.phase, S.t0 = 1, T.now()
      S.ho = out("fujinet_handover")
    elseif T.now() > 90 then
      T.verdict(false, "never left the loader:\n" .. T.screen())
    end
  elseif S.phase == 1 and T.now() > S.t0 + 10 then
    S.h1 = hash(); scr:snapshot(); S.phase = 2
  elseif S.phase == 2 and T.now() > S.t0 + 20 then
    S.h2 = hash(); scr:snapshot()
    local ho = S.ho == 1 and "bios" or "direct"
    -- a title screen waiting for a button is static; blank is not a game
    local b = blank()
    local ok = not b and (not expect or expect == ho)
    T.verdict(ok, string.format("mode %d, hand-over %s, screens %08X %08X%s",
                                out("fujinet_mode"), ho, S.h1, S.h2,
                                b and " (blank)" or S.h1 == S.h2 and " (static)" or ""))
  end
end)
