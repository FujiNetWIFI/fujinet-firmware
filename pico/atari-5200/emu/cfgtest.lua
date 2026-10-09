-- cfgtest.lua -- the real CONFIG, driven with the controller: host slots, the
-- SD host, into a5200/, pacman.bin, boot. PASS when the cart has swapped the
-- game in and the screen is not blank. On the way, when the SD has them, it
-- pages through a7800pg/ (more than one screenful) and reads the info screen.
-- The stick moves, the keypad and fire buttons act, so both are exercised.
-- Every screen is printed.
--
--   A52_SCREEN=0x3C00 ./run.sh config cfgtest   (run.sh sets it for config)
--   CFG_DIR=a5200/ CFG_GAME=bbsb.bin            another game
--   CFG_PAGE=                                    skip paging
--   CFG_EXTRA=1                                  also: rename host 8 on the
--                                                on-screen keyboard and back
local M = dofile(os.getenv("A52_EMU_DIR") .. "/a52text.lua")
if _G.__cfgtest then return end
_G.__cfgtest = true

local function env(name, default)
  local v = os.getenv(name)
  if v == nil then return default end
  return v ~= "" and v or nil
end
local DIR = env("CFG_DIR", "a5200/")
local GAME = env("CFG_GAME", "pacman.bin")
local PAGE = env("CFG_PAGE", "a7800pg/")
local EXTRA = env("CFG_EXTRA", nil)
local LIST_TOP, LIST_END, PATH_ROW = 3, 18, 1

local scr = manager.machine.screens[":screen"]
local ports = manager.machine.ioport.ports
local function field(p, n) return ports[p].fields[n] end
local stick_x = field(":analog_0", "AD Stick X")
local stick_y = field(":analog_1", "AD Stick Y")
local key = {
  fire = field(":djoy_b", "P1 Button 1"), back = field(":djoy_b", "P1 Button 2"),
  hash = field(":keypad.0", "[#]"), star = field(":keypad.0", "[*]"),
  start = field(":keypad.3", "Start"), pause = field(":keypad.2", "Pause"),
  reset = field(":keypad.1", "Reset"),
  k2 = field(":keypad.3", "[2]"), k8 = field(":keypad.1", "[8]"),
  k4 = field(":keypad.2", "[4]"), k6 = field(":keypad.2", "[6]"),
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

-- A button or keypad key: held long enough for the keypad's scan, released
-- long enough to count as a new press.
local function press(k, n)
  key[k]:set_value(1)
  wait(n or 4)
  key[k]:set_value(0)
  wait(6)
end

-- The stick: POT values latch once a frame, so hold three or more.
local function push(dir)
  local f, v = stick_y, 0x00
  if dir == "down" then v = 0xE4 end
  if dir == "left" then f, v = stick_x, 0x00 end
  if dir == "right" then f, v = stick_x, 0xE4 end
  f:set_value(v)
  wait(4)
  f:clear_value()
  wait(6)
end

local function bar() return M.bar(LIST_TOP, LIST_END) end

local function in_list(text)
  return M.find(text, LIST_TOP, LIST_END)
end

-- Move the bar onto the list row holding `text`.
local function pick(text)
  local y = in_list(text)
  if not y then fail("no '" .. text .. "' in the list") end
  for _ = 1, 40 do
    local b = bar()
    if b == y then return end
    if not b then fail("no selection bar") end
    push(b < y and "down" or "up")
    until_(function() return bar() and bar() ~= b end, 5, "the bar to move")
  end
  fail("cannot reach '" .. text .. "'")
end

local function path_shown()
  return (M.row(PATH_ROW):sub(3, 38):gsub("[%s|]+$", ""))
end

-- A directory listing, complete: its path, the bar, and no READING left.
local function listing(path)
  until_(function() return path_shown() == path and bar() ~= nil
                           and not M.find("READING", 20, 20) end,
         30, "the listing of " .. path)
end

local function open_dir(name, path)
  pick(name)
  press("hash")
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
  until_(function() return M.find("HOST SLOTS", 0, 0) and bar() ~= nil end,
         60, "HOST SLOTS")
end

local function open_sd()
  pick("SD")
  press("fire")
  until_(function() return M.find("HOST 1", 0, 0) and bar() ~= nil end,
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
  push("right")
  until_(function() return bar() and M.row(LIST_TOP) ~= first end, 30, "page 2")
  show("/" .. PAGE .. " page 2")
  local second = M.row(LIST_TOP)
  press("k4")                           -- the keypad pages too
  until_(function() return bar() and M.row(LIST_TOP) == first end, 30, "page 1 again")
  print("paging: page 1 starts '" .. first .. "', page 2 starts '" .. second .. "'")
  press("star")
  listing("/")
end

local function info_test()
  press("back")                         -- the lower button: out to the hosts
  hosts_screen()
  press("start")
  until_(function() return M.find("ADAPTER INFO", 0, 0) and M.find("VERSION") end,
         20, "the info screen")
  show("info")
  press("fire")
  hosts_screen()
end

-- The keyboard grid: 16 x 4 cells of $20-$5F, the cursor starting on '@'.
local function type_char(c, at)
  local code = string.byte(c) - 0x20
  local x, y = code % 16, code // 16
  while at.y < y do press("k8"); at.y = at.y + 1 end
  while at.y > y do press("k2"); at.y = at.y - 1 end
  while at.x < x do press("k6"); at.x = at.x + 1 end
  while at.x > x do press("k4"); at.x = at.x - 1 end
  press("hash")
end

local function host8()
  return (M.row(LIST_TOP + 7):sub(6, 38):gsub("[%s|]+$", ""))
end

local function rename8(add, del)
  local want = host8()
  want = add and want .. add or want:sub(1, #want - del)
  pick("8 " .. host8())
  press("pause")
  until_(function() return M.find("HOST NAME?", 0, 2) end, 10, "the editor")
  wait(30)
  if add then
    local at = { x = 0, y = 2 }
    for i = 1, #add do type_char(add:sub(i, i), at) end
  else
    for _ = 1, del do press("star") end
  end
  show("editing host 8")
  press("start")
  until_(function() return host8() == want and bar() end, 20, "host 8 renamed " .. want)
  show("host 8 renamed")
end

local co = coroutine.create(function()
  hosts_screen()
  show("hosts")
  if EXTRA then
    rename8("X")
    rename8(nil, 1)
  end
  open_sd()
  page_test()
  info_test()
  open_sd()
  open_dir(DIR, "/" .. DIR)

  pick(GAME)
  press("fire")
  until_(function() return M.find("LOADING", 0, 0) end, 10, "LOADING")
  show("loading " .. GAME)
  local t0 = M.now()
  while manager.machine.output:get_value("fujinet_swaps") < 1 do
    if M.now() - t0 > 90 then fail("the game never started") end
    coroutine.yield()
  end
  print(string.format("swapped in at %.1f s", M.now()))
  wait(600)
  scr:snapshot()
  local v = variety()
  M.verdict(v > 1, string.format("%s%s: swapped in, %d colours on screen", DIR, GAME, v))
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
