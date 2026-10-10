-- powertest.lua -- a power cycle under fujitest. MAME's soft reset resets the
-- cart and the 1861 but not the 1802 (only its CLEAR line does that), so a
-- soft reset plus CLEAR stands in for console power: the cart comes back
-- serving its boot client with the interlock started over (ACKSEQ 0), and the
-- client's first transaction must go through as SEQ 01 again.
local T = dofile(os.getenv("S2_EMU_DIR") .. "/s2screen.lua")

_G.pt = _G.pt or { phase = 1, t0 = 0 }
local pt = _G.pt

T.every_frame(function()
  local t = T.now()
  local seq = T.find("SEQ ")
  if pt.phase == 1 then
    if seq and T.row(seq):find("SEQ 01", 1, true) then
      print("powertest: first run SEQ 01; power cycle")
      pt.phase, pt.t0 = 2, t
      manager.machine:soft_reset()
      T.clear(3)
    elseif t > 30 then
      T.verdict(false, "first transaction never finished:\n" .. T.screen())
    end
  elseif pt.phase == 2 then
    if t - pt.t0 > 0.02 then
      local magic = T.status(0x09) == 0x46 and T.status(0x0A) == 0x4E
      print(string.format("powertest: after the cycle ACKSEQ %d, magic %s",
                          T.status(0x00), magic and "ok" or "missing"))
      pt.phase = 3
    end
  elseif pt.phase == 3 then
    if seq and t - pt.t0 > 0.5 and T.row(seq):find("SEQ 01", 1, true) then
      T.verdict(T.row(seq):find("ERR 00", 1, true) ~= nil, "after a power cycle: " .. T.row(seq))
    elseif t - pt.t0 > 30 then
      T.verdict(false, "no transaction after the power cycle:\n" .. T.screen())
    end
  end
end)
