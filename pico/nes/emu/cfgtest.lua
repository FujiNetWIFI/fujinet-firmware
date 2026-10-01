-- cfgtest.lua -- drive CONFIG with the joypad: open host slot 1, move the
-- bar to CFG_FILE, boot it, and PASS once CFG_EXPECT is on screen (i.e. the
-- booted image is running out of the SRAM). FAIL on an E-status or timeout.
--   CFG_FILE     entry to boot, paged to with Right if not on page 1 (default hello.bin)
--   CFG_EXPECT   text the booted image shows (default "FUJINET NES"), or
--                "image:<local path>" to byte-compare the PRG and CHR SRAMs
--                with that .nes file instead (a game with no text font)
--   CFG_TIMEOUT  seconds                  (default 90)
local T = dofile(os.getenv("NES_EMU_DIR") .. "/nestext.lua")
local file = os.getenv("CFG_FILE") or "hello.bin"
local expect = os.getenv("CFG_EXPECT") or "FUJINET NES"
local timeout = tonumber(os.getenv("CFG_TIMEOUT") or "90")

-- image:<path> -> the PRG and CHR bytes the SRAMs must end up holding
local image
if expect:sub(1, 6) == "image:" then
  local f = assert(io.open(expect:sub(7), "rb"))
  local d = f:read("a"); f:close()
  local prg = d:byte(5) * 16384
  local chr = d:byte(6) * 8192
  image = { prg = d:sub(17, 16 + prg), chr = d:sub(17 + prg, 16 + prg + chr) }
end

-- does the console's view of the SRAMs equal the image? (reads are inert)
local function image_in_place()
  local cpu, ppu = T.cpu_space(), T.ppu_space()
  for i = 1, #image.prg do
    if cpu:readv_u8(0x8000 + i - 1) ~= image.prg:byte(i) then return false end
  end
  for i = 1, #image.chr do
    if ppu:readv_u8(i - 1) ~= image.chr:byte(i) then return false end
  end
  return true
end
-- cc65's conio hides the top overscan row: conio row N is nametable row N+1.
local LIST_TOP, STATUS_ROW = 5 + 1, 23 + 1

local pad = manager.machine.ioport.ports[":ctrl1:joypad:JOYPAD"]
local function btn(name) return pad.fields["P1 " .. name] end

-- A press is held for 6 frames and released for 6: one event for fujiin.c,
-- which samples once per frame and auto-repeats only after 20 frames. The
-- override takes effect at the next frame's input update, so the Lua read
-- lags set_value by a frame.
local presses, hold = {}, 0
local function press(name, n) for _ = 1, (n or 1) do presses[#presses + 1] = name end end
local function pump()
  if hold > 0 then
    hold = hold - 1
    if hold == 6 then btn(presses[1]):set_value(0); table.remove(presses, 1) end
    return true
  end
  if #presses > 0 then btn(presses[1]):set_value(1); hold = 12; return true end
  return false
end

local step, done, target, pages, first = 1, false, nil, 0, nil
local function fail(msg) done = true; print(T.screen()); T.verdict(false, msg) end

T.every_frame(function()
  if done then return end
  if T.now() > timeout then return fail("timeout at step " .. step) end
  local st = T.row(STATUS_ROW)
  if st:match("^ E%u+%s+%x%x$") then return fail("CONFIG reported " .. st) end
  if pump() then return end

  if step == 1 then
    if T.find("SELECT A HOST") and T.inverted(LIST_TOP) then
      print(string.format("%.2fs hosts up", T.now())); press("A"); step = 2
    end
  elseif step == 2 then
    -- a fresh page: the bar is back on the top row and that row changed
    if T.row(3 + 1) == " /" and T.inverted(LIST_TOP) and T.row(LIST_TOP) ~= first then
      first = T.row(LIST_TOP)
      target = T.find(file)
      if target then
        print(string.format("%.2fs page %d up, %s on row %d", T.now(), pages + 1, file, target))
        press("Down", target - LIST_TOP); step = 3
      else
        pages = pages + 1
        if pages > 4 then return fail(file .. " not within 4 pages") end
        print(string.format("%.2fs page %d up, paging on", T.now(), pages))
        press("Right")
      end
    end
  elseif step == 3 then
    if not T.inverted(target) then return fail("bar did not reach row " .. target) end
    print(string.format("%.2fs bar on %s", T.now(), T.row(target)))
    press("A"); step = 4
  elseif step == 4 then
    if image then
      if image_in_place() then
        done = true
        T.verdict(true, string.format("%d+%d bytes of %s in the SRAMs at %.2fs",
                                      #image.prg, #image.chr, file, T.now()))
      end
    elseif T.find(expect) then
      done = true
      print(T.screen())
      T.verdict(true, string.format("%q booted through CONFIG at %.2fs", expect, T.now()))
    end
  end
end)
