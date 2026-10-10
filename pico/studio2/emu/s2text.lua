-- s2text.lua -- M0: hello through the real BIOS. The client's own ISR points
-- the 1861's DMA at the cart's raster; the cart's text engine draws. Passes
-- when the text is on screen, the frame counter moves, a key press arrives,
-- the raster holds the glyphs, and no DMA line came up short.
local T = dofile(os.getenv("S2_EMU_DIR") .. "/s2screen.lua")

_G.st = _G.st or { phase = 1, t0 = 0 }
local st = _G.st

T.every_frame(function()
  local t = T.now()
  if st.phase == 1 then
    if T.find("HELLO STUDIO II") and T.row(6):find("FRAME") then
      st.f0, st.t0, st.phase = T.row(6), t, 2
      print("s2text: hello is up at " .. string.format("%.2f", t) .. " s")
    elseif t > 10 then
      T.verdict(false, "hello never drew:\n" .. T.screen())
    end
  elseif st.phase == 2 and t - st.t0 > 1 then
    if T.row(6) == st.f0 then
      T.verdict(false, "the frame counter is stuck: " .. st.f0)
    end
    T.press("A", 5, 6)
    st.phase, st.t0 = 3, t
  elseif st.phase == 3 and t - st.t0 > 0.5 then
    -- "A5" is KEY_A + 5; the glyph rows of 'H' (#.#) at row 0, column 1
    local key = T.row(8):find("A5", 1, true)
    local h = T.raster(1 * 8 + 0) & 0x0F
    print(T.screen())
    print(string.format("s2text: raster line 1 byte 0 = %02X, short lines %d",
                        T.raster(8), T.short_lines()))
    T.snap()
    if not key then
      T.verdict(false, "key A5 never arrived")
    elseif h ~= 0x0A then
      T.verdict(false, string.format("raster: 'H' top row reads %X, not A", h))
    elseif T.short_lines() ~= 0 then
      T.verdict(false, T.short_lines() .. " DMA bursts ran short")
    else
      T.verdict(true, "hello, frames counting, key A5, raster and DMA clean")
    end
  end
end)
