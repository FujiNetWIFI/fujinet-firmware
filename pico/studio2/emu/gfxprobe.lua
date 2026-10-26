-- gfxprobe.lua -- protocol 2 (testrom/s2gfx.asm): the bank window, pokes and
-- COPY on the text screen; stamps, rectangles and pixel text in the
-- background; and 16 moving sprites, whole in every shown frame, all moved
-- by the VIEW together. Run under -nothrottle: the device composes just
-- before each frame there, so the sprites Lua sees are the ones shown.
local T = dofile(os.getenv("S2_EMU_DIR") .. "/s2screen.lua")

local function bgpix(xpos, ypos)
  return (T.raster(ypos * 8 + xpos // 8) >> (7 - xpos % 8)) & 1
end

-- The 3x3 ring of image 0 at (xpos, ypos) of the shown picture.
local function ring(xpos, ypos)
  for offy = 0, 2 do
    for offx = 0, 2 do
      if T.pixel(xpos + offx, ypos + offy) ~= ((offx == 1 and offy == 1) and 0 or 1) then return false end
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
    for _, text in ipairs(want) do
      if not T.find(text) then fail = "no '" .. text .. "' on\n" .. T.screen() end
    end
    -- the ship (image 1) ORed at 2,50: its widest line
    for col = 2, 9 do if bgpix(col, 52) ~= 1 then fail = fail or "no stamp at 2,52" end end
    -- dashed lines at 46 and 64, two on two off from x = 0
    for col = 0, 63 do
      local lit = (col % 4 < 2) and 1 or 0
      if bgpix(col, 46) ~= lit or bgpix(col, 64) ~= lit then fail = fail or ("dash at " .. col) end
    end
    -- "GFX" at 41,49: the G's top line, two lines in
    if bgpix(41, 50) ~= 1 or bgpix(43, 50) ~= 1 or bgpix(44, 50) ~= 0 then fail = fail or "no GFX" end
    T.snap()
    if fail then T.verdict(false, fail) end
    return
  end
  -- every frame: 16 whole rings where the sprites are, one shift for all
  local shift
  for num = 0, 15 do
    local sprite = T.spr(num)
    local seen = ring(sprite.xpos, sprite.ypos) and 0
                 or (ring(sprite.xpos + 1, sprite.ypos) and 1 or nil)
    if not sprite.shown or seen == nil or (shift ~= nil and seen ~= shift) then
      T.snap()
      T.verdict(false, string.format("frame %d: sprite %d (%d,%d) not whole or out of step",
                                     frames, num, sprite.xpos, sprite.ypos))
      return
    end
    shift = seen
  end
  shifted[shift] = true
  local xpos = T.spr(0).xpos
  if lastx and xpos ~= lastx then moved = moved + 1 end
  lastx = xpos
  if frames >= 340 then
    local pass = moved > 200 and shifted[0] and shifted[1]
    T.verdict(pass, string.format("banks, pokes, COPY, stamps, rects, text; 300 frames of 16 whole sprites, %d moves, VIEW %s",
                                moved, (shifted[0] and shifted[1]) and "shifts" or "never shifted"))
  end
end)
