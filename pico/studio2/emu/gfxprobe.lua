-- gfxprobe.lua -- protocol 2 (testrom/s2gfx.asm): the bank window, pokes and
-- COPY on the text screen; stamps, rectangles and pixel text in the
-- background; and 16 moving sprites, whole in every shown frame, all moved
-- by the VIEW together. Run under -nothrottle: the device composes just
-- before each frame there, so the sprites Lua sees are the ones shown.
local T = dofile(os.getenv("S2_EMU_DIR") .. "/s2screen.lua")

local function bg(x, y) return (T.raster(y * 8 + x // 8) >> (7 - x % 8)) & 1 end

-- The 3x3 ring of image 0 at (x, y) of the shown picture.
local function ring(x, y)
  for j = 0, 2 do
    for i = 0, 2 do
      if T.pixel(x + i, y + j) ~= ((i == 1 and j == 1) and 0 or 1) then return false end
    end
  end
  return true
end

local frames, moved, shifted, lastx = 0, 0, {}, nil
local fail

T.every_frame(function()
  frames = frames + 1
  if frames < 40 then return end
  if frames == 40 then
    local want = { "PROTO 02", "BANK B0 B1 FF", "PK!", "COPIED" }
    for _, w in ipairs(want) do
      if not T.find(w) then fail = "no '" .. w .. "' on\n" .. T.screen() end
    end
    -- the ship (image 1) ORed at 2,50: its widest line
    for x = 2, 9 do if bg(x, 52) ~= 1 then fail = fail or "no stamp at 2,52" end end
    -- dashed lines at 46 and 64, two on two off from x = 0
    for x = 0, 63 do
      local on = (x % 4 < 2) and 1 or 0
      if bg(x, 46) ~= on or bg(x, 64) ~= on then fail = fail or ("dash at " .. x) end
    end
    -- "GFX" at 41,49: the G's top line, two lines in
    if bg(41, 50) ~= 1 or bg(43, 50) ~= 1 or bg(44, 50) ~= 0 then fail = fail or "no GFX" end
    T.snap()
    if fail then T.verdict(false, fail) end
    return
  end
  -- every frame: 16 whole rings where the sprites are, one shift for all
  local d
  for n = 0, 15 do
    local s = T.spr(n)
    local dn = ring(s.x, s.y) and 0 or (ring(s.x + 1, s.y) and 1 or nil)
    if not s.on or dn == nil or (d ~= nil and dn ~= d) then
      T.snap()
      T.verdict(false, string.format("frame %d: sprite %d (%d,%d) not whole or out of step",
                                     frames, n, s.x, s.y))
      return
    end
    d = dn
  end
  shifted[d] = true
  local x = T.spr(0).x
  if lastx and x ~= lastx then moved = moved + 1 end
  lastx = x
  if frames >= 340 then
    local ok = moved > 200 and shifted[0] and shifted[1]
    T.verdict(ok, string.format("banks, pokes, COPY, stamps, rects, text; 300 frames of 16 whole sprites, %d moves, VIEW %s",
                                moved, (shifted[0] and shifted[1]) and "shifts" or "never shifted"))
  end
end)
