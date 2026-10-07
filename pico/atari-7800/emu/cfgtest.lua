-- cfgtest.lua -- the real CONFIG, driven with the joystick: host slots, the
-- SD host, into a7800/, asteroid.bin, boot. PASS when the cart reports the
-- game running (mode 2) after the console's own BIOS started it (hand-over
-- 1) and the screen is not blank. On the way, when the SD has them, it pages
-- through a7800pg/ (more than one screenful), installs a7800/hsc.bin as the
-- High Score Cart, and reads the info screen. Every screen is printed.
--
--   ./run.sh config cfgtest          after `make install` in fujinet-config atari7800/
--   CFG_DIR=a7800/ CFG_GAME=rampage.bin   another game
--   CFG_EXPECT=direct                the hand-over the cart should choose
--   CFG_PAGE= CFG_HSC=               skip paging, skip the HSC
--   CFG_EXTRA=1                      also: rename host 8 on the on-screen
--                                    keyboard and back, turn the HSC off and
--                                    on, copy the game into CFG_PAGE
local M = dofile(os.getenv("A78_EMU_DIR") .. "/mtext.lua")
if _G.__cfgtest then return end
_G.__cfgtest = true

-- unset: the default; set but empty: skip that step
local function env(name, default)
  local v = os.getenv(name)
  if v == nil then return default end
  return v ~= "" and v or nil
end
local DIR = env("CFG_DIR", "a7800/")
local GAME = env("CFG_GAME", "asteroid.bin")
local PAGE = env("CFG_PAGE", "a7800pg/")
local HSC = env("CFG_HSC", "hsc.bin")
local EXPECT = env("CFG_EXPECT", "bios")
local EXTRA = env("CFG_EXTRA", nil)
local LIST_TOP, LIST_END, PATH_ROW = 3, 18, 1

local mode_out = manager.machine.devices[":"]:output("fujinet_mode")
local ho_out = manager.machine.devices[":"]:output("fujinet_handover")
local scr = manager.machine.screens[":screen"]
local ports = manager.machine.ioport.ports
local function field(p, n) return ports[p].fields[n] end
local key = {
  up = field(":JOYSTICKS", "P1 Up"), down = field(":JOYSTICKS", "P1 Down"),
  left = field(":JOYSTICKS", "P1 Left"), right = field(":JOYSTICKS", "P1 Right"),
  fire = field(":BUTTONS", "P1 Button 1"), back = field(":BUTTONS", "P1 Button 2"),
  select = field(":CONSOLE", "Select"), pause = field(":CONSOLE", "Pause"),
  reset = field(":CONSOLE", "Reset"),
}

local function wait(n) for _ = 1, n do coroutine.yield() end end

local function show(title)
  print("---- " .. title .. string.format(" (%.1f s) ----", M.now()))
  print(M.screen())
end

local function fail(msg)
  show("FAILED HERE")
  M.verdict(false, msg)
  while true do coroutine.yield() end
end

-- `pred` true for three frames running: the program draws between frames.
local function until_(pred, secs, what)
  local t0, run = M.now(), 0
  while run < 3 do
    run = pred() and run + 1 or 0
    if M.now() - t0 > secs then fail("timed out waiting for " .. what) end
    coroutine.yield()
  end
end

local function press(k, n)
  key[k]:set_value(1)
  wait(n or 3)
  key[k]:set_value(0)
  wait(4)
end

local function in_list(text)
  return M.find(text, LIST_TOP, LIST_END)
end

-- Move the bar onto the list row holding `text`.
local function pick(text)
  local y = in_list(text)
  if not y then fail("no '" .. text .. "' in the list") end
  for _ = 1, 40 do
    local b = M.bar()
    if b == y then return end
    if not b then fail("no selection bar") end
    press(b < y and "down" or "up")
    until_(function() return M.bar() and M.bar() ~= b end, 5, "the bar to move")
  end
  fail("cannot reach '" .. text .. "'")
end

local function path_shown()
  return (M.row(PATH_ROW):sub(3, 30):gsub("[%s|]+$", ""))
end

-- A directory listing, complete: its path on row 1 and the bar drawn.
local function listing(path)
  until_(function() return path_shown() == path and M.bar() ~= nil end,
         30, "the listing of " .. path)
end

local function open_dir(name, path)
  pick(name)
  press("fire")
  listing(path)
  show(path)
end

local function variety()
  local px = scr:pixels()
  local seen, n = {}, 0
  for i = 1, #px - 3, 4 * 3 do
    local v = px:sub(i, i + 3)
    if not seen[v] then seen[v], n = true, n + 1 end
  end
  return n
end

local function hosts_screen()
  until_(function() return M.find("HOST SLOTS", 0, 0) and M.bar() ~= nil end,
         60, "HOST SLOTS")
end

local function open_sd()
  pick("SD")
  press("fire")
  until_(function() return M.find("HOST 1", 0, 0) and M.bar() ~= nil end,
         30, "the SD host's root")
  show("SD host, /")
end

local function page_test()
  if not PAGE then return end
  if not in_list(PAGE) then
    print("(no " .. PAGE .. " on the SD: paging not exercised)")
    return
  end
  open_dir(PAGE, "/" .. PAGE)
  local first = M.row(LIST_TOP)
  press("right")
  until_(function() return M.bar() and M.row(LIST_TOP) ~= first end, 30, "page 2")
  show("/" .. PAGE .. " page 2")
  local second = M.row(LIST_TOP)
  press("left")
  until_(function() return M.bar() and M.row(LIST_TOP) == first end, 30, "page 1 again")
  print("paging: page 1 starts '" .. first .. "', page 2 starts '" .. second .. "'")
  press("back")
  listing("/")
end

local function hsc_test()
  if not HSC then return end
  if not in_list(HSC) then
    print("(no " .. DIR .. HSC .. " on the SD: HSC install not exercised)")
    return
  end
  pick(HSC)
  press("fire")
  until_(function() return M.find("USE AS HIGH SCORE CART") end, 90, "the HSC offer")
  show("HSC ROM pushed")
  press("fire")
  until_(function() return M.find("HIGH SCORE CART INSTALLED") end, 10, "the HSC install")
  show("HSC installed")
  if M.status(0x26) & 0x03 ~= 0x03 then
    fail(string.format("HSC status $%02X: not installed and on", M.status(0x26)))
  end
  listing("/" .. DIR)
end

local function info_test()
  press("back")                         -- up to /
  listing("/")
  press("back")                         -- out to the hosts
  hosts_screen()
  press("pause")
  until_(function() return M.find("ADAPTER INFO", 0, 0) and M.find("INPTCTRL") end,
         20, "the info screen")
  show("info")
  press("fire")
  hosts_screen()
end

-- The keyboard grid: 16 x 4 cells of $20-$5F, the cursor starting on '@'.
local function type_char(c, at)
  local code = string.byte(c) - 0x20
  local x, y = code % 16, code // 16
  while at.y < y do press("down"); at.y = at.y + 1 end
  while at.y > y do press("up"); at.y = at.y - 1 end
  while at.x < x do press("right"); at.x = at.x + 1 end
  while at.x > x do press("left"); at.x = at.x - 1 end
  press("fire")
end

-- Host 8's name, from its row on the hosts screen.
local function host8()
  return (M.row(LIST_TOP + 7):sub(6, 30):gsub("[%s|]+$", ""))
end

-- Rename host 8 on the keyboard: type `add`, or delete `del` characters.
local function rename8(add, del)
  local want = host8()
  want = add and want .. add or want:sub(1, #want - del)
  pick("8 " .. host8())
  press("select")
  -- the blinking cursor is the last thing the editor draws
  until_(function() return M.find("HOST NAME?", 0, 2) and M.row(2):find("#", 1, true) end,
         10, "the editor")
  if add then
    local at = { x = 0, y = 2 }
    for i = 1, #add do type_char(add:sub(i, i), at) end
  else
    for _ = 1, del do press("back") end
  end
  show("editing host 8")
  press("pause")
  until_(function() return host8() == want and M.bar() end, 20, "host 8 renamed " .. want)
  show("host 8 renamed")
end

local function hsc_toggle()
  if M.status(0x26) & 0x01 == 0 then
    print("(no HSC installed: the on/off switch not exercised)")
    return
  end
  press("pause")
  until_(function() return M.find("INPTCTRL") end, 20, "the info screen")
  local before = M.status(0x26)
  press("select")
  until_(function() return M.status(0x26) ~= before end, 5, "the HSC to switch")
  show("info, HSC switched")
  press("select")
  until_(function() return M.status(0x26) == before end, 5, "the HSC to switch back")
  print(string.format("HSC status $%02X -> $%02X -> $%02X", before,
                      before ~ 0x02, M.status(0x26)))
  press("fire")
  hosts_screen()
end

local function copy_test()
  open_sd()
  open_dir(DIR, "/" .. DIR)
  pick(GAME)
  press("pause")
  until_(function() return M.find("COPY TO WHICH HOST?", 0, 0) and M.bar() end,
         20, "the copy destination picker")
  show("copy: pick a host")
  pick("SD")
  press("fire")
  until_(function() return M.find("HOST 1", 0, 0) and M.bar() end, 30, "the destination root")
  open_dir(PAGE, "/" .. PAGE)
  press("pause")
  until_(function() return M.find("COPIED") or M.find("ERROR") end, 90, "the copy")
  show("copy done")
  if not M.find("COPIED") then fail("the copy failed") end
  listing("/" .. DIR)
  press("reset")                        -- RESET is button 2 too
  listing("/")
  press("back")
  hosts_screen()
end

local function extras()
  rename8("X")
  rename8(nil, 1)
  hsc_toggle()
  copy_test()
end

local co = coroutine.create(function()
  hosts_screen()
  show("hosts")
  open_sd()
  page_test()
  open_dir(DIR, "/" .. DIR)
  hsc_test()
  info_test()
  if EXTRA then extras() end
  open_sd()
  open_dir(DIR, "/" .. DIR)

  pick(GAME)
  press("fire")
  until_(function() return M.find("LOADING", 0, 0) end, 10, "LOADING")
  show("loading " .. GAME)
  local t0 = M.now()
  while mode_out:get() ~= 2 do
    if M.now() - t0 > 90 then fail("the game never started") end
    coroutine.yield()
  end
  local ho = ho_out:get()
  print(string.format("game running at %.1f s, hand-over %d (%s)", M.now(), ho,
                      ho == 1 and "BIOS" or ho == 2 and "direct" or "none"))
  wait(300)
  scr:snapshot()
  local v = variety()
  local want = EXPECT == "direct" and 2 or 1
  M.verdict(ho == want and v > 1,
            string.format("%s%s: mode 2, hand-over %s, %d colours on screen",
                          DIR, GAME, ho == 1 and "BIOS" or ho == 2 and "direct"
                          or tostring(ho), v))
  while true do coroutine.yield() end
end)

M.every_frame(function()
  if coroutine.status(co) == "dead" then return end
  local ok, err = coroutine.resume(co)
  if not ok then
    print("cfgtest: " .. tostring(err))
    manager.machine:exit()
  end
end)
