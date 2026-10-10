-- cfgtest.lua -- the real CONFIG (fujinet-config studio2/), driven from the
-- keypad against a live fujinet-pc whose SD holds /studio2/.
--
--   default      status screen, host slots, open slot 1 (SD), into /studio2/,
--                page forward and back, the bar onto CFG_GAME (a name long
--                enough to scroll), boot it: the cart swaps and the game's
--                frames keep changing
--   CFG_EXTRA=1  rename the last host slot through the keyboard, check the
--                re-read list shows it, then rename it back
--   CFG_EXTRA=2  adapter info; the network list paged, a network and its
--                password, connect; <OTHER> and cancel; a boot the cart
--                refuses (the 2K BIOS dump); copy a file to slot 1's root
--                and find it there (the caller deletes it, and restores
--                fnconfig.ini: connecting saves the network)
--   CFG_EXTRA=3  boot a FujiNet app from /studio2/ (CFG_GAME, default 5card:
--                5 Card Stud's 5card.st2), let it reach its live lobby, quit
--                from its menu: the cart must swap the baked CONFIG back in
--                (the caller removes the name appkey it saved)
--
-- Every press is held 6 frames, and the next waits for the screen to answer
-- it and for the key to be released.
local T = dofile(os.getenv("S2_EMU_DIR") .. "/s2screen.lua")

local MODE = os.getenv("CFG_EXTRA") or "0"
local GAME = os.getenv("CFG_GAME") or (MODE == "3" and "5card" or "Space Invaders")
local WATCH = tonumber(os.getenv("BOOT_SECS") or "6")
local EXTRA = MODE ~= "0"

_G.ct = _G.ct or { i = 1, t0 = nil, gap = 0, v = {} }
local ct = _G.ct
local V = ct.v

local function say(fmt, ...) print(string.format("cfgtest: " .. fmt, ...)) end
local function press(k) T.press("A", k, 6) end
local function title() return T.row(0) end
-- The browser's title: the path, less the page arrow in its last cell.
local function path() return (T.row(0):sub(1, 15):gsub("%s+$", "")) end
local function bar() return T.bar(1, 8, 2) end

-- The row (1-8) whose text holds s.
local function list_row(s) return T.find(s, 1, 8) end

-- Rendered pixels, for watching a game that draws from console RAM.
local function frame_sig()
  local scr = manager.machine.screens[":screen"]
  local px = scr:pixels()
  local h = 0
  for i = 1, #px, 61 do h = (h * 31 + px:byte(i)) % 1000000007 end
  return h
end

-- A step: act() once, then done() each frame until true (or `tmo` seconds).
local function step(name, act, done, tmo) return { name = name, act = act, done = done, tmo = tmo or 20 } end

-- Press `k` until done() holds, at most n times: each press waits for sig()
-- to change (the screen's answer), or 2 s. done() may name the key instead.
local function press_until(name, k, done, n, sig)
  return { name = name, multi = true, key = k, done = done, n = n or 12, tmo = 30, sig = sig or bar }
end

-- Move the bar onto the row holding `text`, found before the bar gets there:
-- on the bar a long name scrolls, and stops matching.
local function bar_onto(text, k)
  return press_until("bar onto " .. text, k or 8, function()
    V.tgt = V.tgt or list_row(text)
    return V.tgt ~= nil and bar() == V.tgt
  end)
end

local boot_steps = {
  step("status screen", nil, function()
    return T.find("FUJINET CONFIG") and T.find("WIFI CONNECTED") and T.row(3) ~= ""
  end, 40),
  step("snap status", function() T.snap() end, function() return true end),
  step("open the host slots", function() press(5) end, function()
    return title():find("HOST SLOTS", 1, true) and T.row(1):find("1SD", 1, true) and bar() == 1
  end),
  step("snap hosts", function() T.snap() end, function() return true end),
  step("open slot 1 (SD)", function() press(5) end, function()
    return path() == "/" and list_row("studio2") ~= nil
  end),
  bar_onto("studio2"),
  step("into /studio2/", function() press(5) end, function()
    return path() == "/studio2/" and T.row(1) ~= "" and bar() == 1
  end),
  step("note page 1", function()
    V.p1 = T.row(1)
    T.snap()
    say("page 1:\n%s", T.screen())
  end, function() return true end),
  step("next page (6)", function() press(6) end, function()
    return T.row(1) ~= V.p1 and T.row(1) ~= "" and bar() == 1
  end),
  step("note page 2", function()
    V.p2 = T.row(1)
    T.snap()
    say("page 2:\n%s", T.screen())
  end, function() return true end),
  step("previous page (4)", function() press(4) end, function() return T.row(1) == V.p1 end),
  bar_onto(GAME),
  step("the name scrolls", function() V.m0 = T.row(bar()); V.mt = T.now() end, function()
    return T.now() - V.mt > 0.6 and T.row(bar()) ~= V.m0
  end, 5),
  step("snap the bar", function()
    T.snap()
    say("marquee: '%s' -> '%s'", V.m0, T.row(bar()))
  end, function() return true end),
  step("boot it", function() press(5) end, function()
    if T.find("ERROR") then
      T.verdict(false, "boot failed: " .. T.row(T.find("ERROR")) .. "\n" .. T.screen())
    end
    return T.output("fujinet_swaps") >= 1
  end, 90),
  step("the game runs", function()
    V.t0, V.sig, V.changes = T.now(), frame_sig(), 0
    say("swapped at %.2f s", V.t0)
  end, function()
    local s = frame_sig()
    if s ~= V.sig then V.changes = V.changes + 1; V.sig = s end
    return T.now() - V.t0 > WATCH
  end, WATCH + 5),
}

-- OSK: cell = character - $20, at row 3 + cell / 16, column cell % 16.
-- Row 1 is the text, then the selected key as an inverse block cursor.
local function edit_line()
  for c = 0, 15 do
    if T.inv(1, c) then return T.row(1):sub(1, c) end
  end
  return T.row(1)
end

local function osk_cell()
  for r = 3, 8 do
    for c = 0, 15 do
      if T.inv(r, c) then return (r - 3) * 16 + c end
    end
  end
end

local function type_steps(s)
  local out = {}
  for ch in s:gmatch(".") do
    local want = ch:byte() - 0x20
    out[#out + 1] = press_until("cursor to row of '" .. ch .. "'", 0, function()
      local c = osk_cell()
      if c and (c >> 4) == (want >> 4) then return true end
      return false, (c and c >> 4 < want >> 4) and 8 or 2
    end, 8, osk_cell)
    out[#out + 1] = press_until("cursor to '" .. ch .. "'", 0, function()
      local c = osk_cell()
      if c == want then return true end
      return false, (c and c < want) and 6 or 4
    end, 16, osk_cell)
    out[#out + 1] = step("type '" .. ch .. "'", function() V.line = edit_line(); press(5) end,
      function() return edit_line() == V.line .. ch end)
  end
  return out
end

local SUFFIX = "-S2"
local rename_steps = {
  boot_steps[1],
  boot_steps[3],
  press_until("bar onto slot 8", 8, function() return bar() == 8 end),
  step("rename slot 8 (7)", function()
    V.orig = T.row(8):sub(2)
    say("slot 8 is '%s'", V.orig)
    press(7)
  end, function()
    return title() == "HOST NAME" and edit_line() == V.orig and osk_cell() ~= nil
  end),
}
for _, s in ipairs(type_steps(SUFFIX)) do rename_steps[#rename_steps + 1] = s end
for _, s in ipairs({
  step("snap the keyboard", function() T.snap(); say("keyboard:\n%s", T.screen()) end,
    function() return true end),
  step("done (9)", function() press(9) end, function()
    return title():find("HOST SLOTS", 1, true) and T.row(8) == "8" .. V.orig .. SUFFIX
  end),
  step("snap renamed", function() T.snap(); say("renamed:\n%s", T.screen()) end,
    function() return true end),
  step("rename it back (7)", function() press(7) end, function()
    return title() == "HOST NAME" and edit_line() == V.orig .. SUFFIX
  end),
  press_until("delete the suffix (0)", 0, function()
    if edit_line() == V.orig then return true end
    return false, 0
  end, #SUFFIX + 2, edit_line),
  step("done (9)", function() press(9) end, function()
    return title():find("HOST SLOTS", 1, true) and T.row(8) == "8" .. V.orig
  end),
}) do rename_steps[#rename_steps + 1] = s end

local COPIED = "Combat (2000)(Paul Robson).st2"
local tour_steps = {
  boot_steps[1],
  boot_steps[3],
  step("adapter info (3)", function() press(3) end, function()
    return title() == "ADAPTER INFO" and T.row(2) == "Dummy Cafe" and T.row(8) == "D01CEDC0FFEE"
  end),
  step("snap info", function() T.snap(); say("info:\n%s", T.screen()) end, function() return true end),
  step("back (5)", function() press(5) end, function() return title() == "HOST SLOTS" end),
  step("networks (1)", function() press(1) end, function()
    return title() == "WIFI NETWORKS" and T.row(1) == " Dummy Cafe 0" and T.row(8) == " Dummy Cafe 7" and bar() == 1
  end),
  step("snap networks", function() T.snap() end, function() return true end),
  step("next page (6)", function() press(6) end, function() return T.row(1) == " Dummy Cafe 8" end),
  step("last page (6)", function() press(6) end, function()
    return T.row(1) == " <OTHER>" and T.row(2) == "" and bar() == 1
  end),
  step("back a page (4)", function() press(4) end, function() return T.row(8) == " Dummy Cafe 15" end),
  bar_onto("Dummy Cafe 11"),
  step("pick it (5)", function() press(5) end, function()
    return title() == "PASSWORD" and edit_line() == "" and osk_cell() ~= nil
  end),
}
for _, s in ipairs(type_steps("pw")) do tour_steps[#tour_steps + 1] = s end
for _, s in ipairs({
  step("connect (9)", function() press(9) end, function()
    return title() == "HOST SLOTS" and T.row(1):find("1SD", 1, true)
  end, 40),
  step("networks again (1)", function() press(1) end, function()
    return title() == "WIFI NETWORKS" and T.row(1) == " Dummy Cafe 0"
  end),
  step("page 2 (6)", function() press(6) end, function() return T.row(1) == " Dummy Cafe 8" end),
  step("page 3 (6)", function() press(6) end, function() return T.row(1) == " <OTHER>" end),
  step("<OTHER> (5)", function() press(5) end, function()
    return title() == "NETWORK NAME" and edit_line() == ""
  end),
  step("cancel (1)", function() press(1) end, function() return T.find("FUJINET CONFIG") ~= nil end),
  step("hosts (5)", function() press(5) end, function() return title() == "HOST SLOTS" end),
  step("open slot 1 (SD)", function() press(5) end, function()
    return path() == "/" and list_row("studio2") ~= nil and list_row(COPIED) == nil
  end),
  bar_onto("studio2"),
  step("into /studio2/", function() press(5) end, function() return path() == "/studio2/" and bar() == 1 end),
  bar_onto("RCA Studio"),
  step("boot it: refused (5)", function() V.bios = bar(); press(5) end, function()
    -- 07 the stage wait, 01 FN_BOOT_ERR_TOOBIG: FAILED seen, not waited out
    return T.row(9):find("ERROR", 1, true) ~= nil
  end, 30),
  step("refused as too big", nil, function()
    if T.row(9):find("ERROR 07:01", 1, true) then return true end
    T.verdict(false, "the refusal reads '" .. T.row(9) .. "', not 07:01")
  end),
  step("snap the error", function() T.snap(); say("refused: %s", T.row(9)) end, function() return true end),
  step("any key (5)", function() press(5) end, function()
    return path() == "/studio2/" and T.row(9) == "5:GO 0:UP 7:COPY" and bar() == V.bios
  end),
  bar_onto("Combat", 2),
  step("copy Combat (7)", function() V.r = bar(); press(7) end, function()
    return title() == "COPY TO HOST" and T.row(9):find("CANCEL", 1, true)
  end),
  step("to slot 1 (5)", function() press(5) end, function()
    if T.find("ERROR", 9, 9) then T.verdict(false, "copy failed: " .. T.row(9)) end
    return T.row(9):find("COPIED", 1, true)
  end, 30),
  step("any key (5)", function() press(5) end, function() return path() == "/studio2/" and bar() == V.r end),
  step("up (0)", function() press(0) end, function() return path() == "/" and list_row("Combat") ~= nil end),
  step("snap root", function() T.snap(); say("root after the copy:\n%s", T.screen()) end, function() return true end),
  step("up again (0)", function() press(0) end, function() return title() == "HOST SLOTS" end),
}) do tour_steps[#tour_steps + 1] = s end

-- CFG_EXTRA=3: CONFIG -> a FujiNet app -> CONFIG, both hand-overs through the
-- BIOS. A short name does not scroll, so the marquee steps go.
local app_steps = {}
for _, s in ipairs(boot_steps) do
  if s.name ~= "the name scrolls" and s.name ~= "snap the bar" and s.name ~= "the game runs" then
    app_steps[#app_steps + 1] = s
  end
end
for _, s in ipairs({
  step("5 Card Stud is up", nil, function() return T.find("YOUR NAME") or T.find("1-8 JOIN") end, 60),
  step("a name (5 types)", function() if T.find("YOUR NAME") then press(5) end end, function()
    return not T.find("YOUR NAME") or T.row(3):gsub("[%s_]", "") ~= ""
  end),
  step("name done (3)", function() if T.find("YOUR NAME") then press(3) end end,
       function() return T.find("1-8 JOIN") end, 60),
  step("snap the lobby", function() T.snap(); say("lobby:\n%s", T.screen()) end, function() return true end),
  step("its menu (0)", function() press(0) end, function() return T.find("4 QUIT TO CONFIG") end),
  step("quit to CONFIG (4)", function() press(4) end, function()
    return T.output("fujinet_swaps") >= 2 and T.find("FUJINET CONFIG")
  end, 40),
}) do app_steps[#app_steps + 1] = s end

local steps = MODE == "1" and rename_steps or MODE == "2" and tour_steps
              or MODE == "3" and app_steps or boot_steps

T.every_frame(function()
  local s = steps[ct.i]
  if not s then return end
  local t = T.now()
  -- the previous press released, and a frame for the CPU to see it
  if #T.held > 0 then ct.gap = 3; return end
  if ct.gap > 0 then ct.gap = ct.gap - 1; return end
  if not ct.t0 then
    ct.t0, ct.n, ct.last, ct.tp, V.tgt = t, 0, nil, t, nil
    if s.act then s.act() end
    if not s.multi then return end
  end
  local ok, k = s.done()
  if ok then
    ct.i, ct.t0 = ct.i + 1, nil
    say("%-24s ok at %.2f s", s.name, t)
    if ct.i > #steps then
      if MODE == "3" then
        T.verdict(T.short_lines() == 0, string.format("CONFIG booted %s, it reached its live lobby, and quitting "
                                       .. "swapped CONFIG back in (%d swaps, %d short DMA lines)",
                                       GAME, T.output("fujinet_swaps"), T.short_lines()))
      elseif MODE == "2" then
        T.verdict(true, "info, networks paged, password, connect, <OTHER>, a refused boot, copy to slot 1's root, up twice")
      elseif EXTRA then
        T.verdict(true, string.format("slot 8 renamed to '%s' through the keyboard, re-read, and restored to '%s'",
                                      V.orig .. SUFFIX, V.orig))
      else
        print(T.screen())
        T.snap()
        local ok2 = V.changes >= 3 and T.short_lines() == 0
        T.verdict(ok2, string.format("booted '%s' from /studio2/ (pages '%s' / '%s'); %d frame changes in %d s, %d short DMA lines",
                                     GAME, V.p1, V.p2, V.changes, WATCH, T.short_lines()))
      end
    end
    return
  end
  if s.multi and (ct.n == 0 or ct.last ~= s.sig() or t - ct.tp > 2) then
    -- the screen answered the last press (or there was none yet): press again
    if ct.n >= s.n then T.verdict(false, s.name .. ": no luck after " .. s.n .. " presses\n" .. T.screen()) end
    ct.n, ct.last, ct.tp = ct.n + 1, s.sig(), t
    press(k or s.key)
    return
  end
  if t - ct.t0 > s.tmo then
    T.verdict(false, string.format("%s: timed out at %.2f s\n%s", s.name, t, T.screen()))
  end
end)
