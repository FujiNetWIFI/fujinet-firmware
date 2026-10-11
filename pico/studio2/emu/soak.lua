-- soak.lua -- Tier C: one image over the network. fujiboot pushes it from the
-- live fujinet-pc and hands over; the device dumps the view it swapped in
-- ($FUJINET_VIEWDUMP, compared with tools/s2plan by tools/soak.py). The RCA
-- carts show nothing until keys pick and start a game, so the keypad script
-- Tier A uses (emu/abtest.lua) plays from the swap; snapshots at 30 s and
-- 60 s after it show the game is on screen.
local T = dofile(os.getenv("S2_EMU_DIR") .. "/s2screen.lua")

local script = {            -- {second after the swap, pad, key}
  { 3, "A", 1 }, { 5, "A", 2 }, { 7, "B", 1 }, { 9, "A", 5 }, { 11, "B", 5 },
  { 13, "A", 4 }, { 15, "A", 6 }, { 17, "B", 8 }, { 19, "A", 0 }, { 21, "B", 2 },
  { 25, "A", 8 }, { 29, "B", 4 }, { 33, "A", 3 }, { 37, "B", 6 }, { 41, "A", 9 },
  { 45, "B", 0 }, { 49, "A", 7 }, { 53, "B", 9 }, { 57, "A", 5 },
}
_G.sk = _G.sk or { phase = 1, t0 = 0, step = 1, shots = 0 }
local sk = _G.sk

T.every_frame(function()
  local t = T.now()
  if sk.phase == 1 then
    if T.output("fujinet_swaps") >= 1 then
      sk.phase, sk.t0 = 2, t
    elseif T.find("FAILED") then
      T.verdict(false, "fujiboot: " .. T.row(T.find("FAILED")))
    elseif t > 90 then
      T.verdict(false, "never swapped:\n" .. T.screen())
    end
    return
  end
  local s = script[sk.step]
  if s and t - sk.t0 >= s[1] then
    T.press(s[2], s[3], 8)
    sk.step = sk.step + 1
  end
  if sk.shots == 0 and t - sk.t0 >= 30 or sk.shots == 1 and t - sk.t0 >= 60 then
    T.snap()
    sk.shots = sk.shots + 1
    if sk.shots == 2 then
      T.verdict(true, string.format("swapped at %.2f s; %d short DMA lines", sk.t0, T.short_lines()))
    end
  end
end)
