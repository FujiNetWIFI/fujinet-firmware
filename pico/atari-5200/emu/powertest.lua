-- powertest.lua -- a power cycle under fujitest. The 5200 has no reset line,
-- so MAME's reset stands in for the power switch: the cart must come back
-- serving its boot image with the interlock started over (ACKSEQ 0), and
-- the client's next transaction must go through.
local T = dofile(os.getenv("A52_EMU_DIR") .. "/a52text.lua")

_G.pt = _G.pt or { phase = 1, t0 = 0 }
local pt = _G.pt

T.every_frame(function()
  local t = T.now()
  if pt.phase == 1 then
    if T.status(0x00) == 1 and T.find("SSID") and T.row(5):find("SSID") then
      print("powertest: first run done, ACKSEQ 1; power cycle")
      pt.phase, pt.t0 = 2, t
      manager.machine:soft_reset()
    elseif t > 20 then
      T.verdict(false, "first transaction never finished:\n" .. T.screen())
    end
  elseif pt.phase == 2 then
    -- straight after the cycle the cart has repainted: magic, ACKSEQ 0
    if t - pt.t0 > 0.05 then
      local ok = T.status(0x09) == 0x46 and T.status(0x0A) == 0x4E
      print(string.format("powertest: after the cycle ACKSEQ %d, magic %s",
                          T.status(0x00), ok and "ok" or "missing"))
      pt.phase = 3
    end
  elseif pt.phase == 3 then
    if T.status(0x00) == 1 and T.row(5):find("SSID") and t - pt.t0 > 1 then
      T.verdict(true, "transaction after a power cycle: " .. T.row(5))
    elseif t - pt.t0 > 20 then
      T.verdict(false, "no transaction after the power cycle:\n" .. T.screen())
    end
  end
end)
