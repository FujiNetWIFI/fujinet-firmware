-- boottest.lua -- M2: fujiboot pushes an image over the network and hands the
-- console to it through the BIOS. Passes when the cart has swapped once and
-- the game's screen keeps changing afterwards. BOOT_SECS (default 8) is how
-- long to watch.
local T = dofile(os.getenv("S2_EMU_DIR") .. "/s2screen.lua")

_G.bt = _G.bt or { phase = 1, t0 = 0, shots = 0 }
local bt = _G.bt
local watch = tonumber(os.getenv("BOOT_SECS") or "8")

T.every_frame(function()
  local t = T.now()
  if bt.phase == 1 then
    if T.output("fujinet_swaps") >= 1 then
      print(string.format("boottest: swapped at %.2f s", t))
      bt.phase, bt.t0 = 2, t
    elseif T.find("FAILED") then
      T.verdict(false, "fujiboot: " .. T.row(T.find("FAILED")))
    elseif t > 90 then
      T.verdict(false, "never swapped:\n" .. T.screen())
    end
  elseif bt.phase == 2 and t - bt.t0 > watch then
    T.snap()
    T.verdict(true, string.format("booted; %d short DMA lines", T.short_lines()))
  end
end)
