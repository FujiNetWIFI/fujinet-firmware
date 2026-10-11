-- resettest.lua -- CLEAR under fujitest. CLEAR resets only the CPU (and the
-- 1861): the cart keeps its ACKSEQ, so after the BIOS boots fujitest again its
-- transaction must go out as SEQ 02, not collide with the answered SEQ 01.
local T = dofile(os.getenv("S2_EMU_DIR") .. "/s2screen.lua")

_G.rt = _G.rt or { phase = 1, t0 = 0 }
local rt = _G.rt

T.every_frame(function()
  local t = T.now()
  local seq = T.find("SEQ ")
  if rt.phase == 1 then
    if seq and T.row(seq):find("SEQ 01", 1, true) then
      print("resettest: first run SEQ 01; CLEAR")
      T.clear(3)
      rt.phase, rt.t0 = 2, t
    elseif t > 30 then
      T.verdict(false, "first run never finished:\n" .. T.screen())
    end
  elseif rt.phase == 2 then
    if seq and t - rt.t0 > 0.5 and T.row(seq):find("SEQ 02", 1, true) then
      print(T.screen())
      T.verdict(T.row(seq):find("ERR 00", 1, true) ~= nil, "after CLEAR: " .. T.row(seq))
    elseif t - rt.t0 > 30 then
      T.verdict(false, "no SEQ 02 after CLEAR:\n" .. T.screen())
    end
  end
end)
