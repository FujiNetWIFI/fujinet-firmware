-- hscram.lua -- the High Score Cart's RAM survives a power-off.
--
-- fujiboot boots a game with the HSC on. 12 s into the game (the cart saves
-- dirty chunks 2 s after the last write) the HSC RAM is printed as one hex
-- line; run it twice, in two MAME processes, and the second must print what
-- the first did: the cart restored it from the FujiNet's AppKeys. HSCRAM0 is
-- the same RAM as the game starts, before it has touched it.
local T = dofile(os.getenv("A78_EMU_DIR") .. "/a78text.lua")
if _G.__hscram then return end
_G.__hscram = { loads = 0, prev = 0 }
local S = _G.__hscram
local function out(name) return manager.machine.devices[":"]:output(name):get() end

local function dump(tag)
  local s, h = T.space(), {}
  for a = 0x1000, 0x17FF do h[#h + 1] = string.format("%02X", s:read_u8(a)) end
  print(tag .. " " .. table.concat(h))
end

T.every_frame(function()
  local mode = out("fujinet_mode")
  if not S.t0 then
    if S.prev == 1 and mode == 2 then S.t0 = T.now(); dump("HSCRAM0") end
    S.prev = mode
    if T.now() > 90 then T.verdict(false, "the game never started") end
    return
  end
  if T.now() > S.t0 + 12 then
    dump("HSCRAM")
    manager.machine:exit()
  end
end)
