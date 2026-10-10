-- 5carddrive.lua -- 5 Card Stud (fujinet-5cardstud studio2/) end to end
-- against the live server, through the cart's fujimail and a fujinet-pc:
-- type a name on the on-screen grid (when the appkey has none), list the
-- real tables, sit at one, play legal moves through two whole hands, then
-- 0 > 4 back to CONFIG and watch the cart take the hand-over.
--
--   SECS=900 MAMEBIN=./studio2-agents FUJINET_TCP=127.0.0.1:9972 \
--       ./run.sh 5card 5carddrive
--
-- Env: S2_NAME (default S2TEST), S2_TABLE (the lobby row to join, default
-- "AI ROOM - 2"), S2_HANDS (default 2), S2_LIMIT (seconds, default 840).
-- Runs throttled: the server's clocks (bot turns, the move timer) are real
-- time, so an unthrottled console would only poll it harder.
local T = dofile(os.getenv("S2_EMU_DIR") .. "/s2screen.lua")
manager.machine.video.throttled = true

local NAME = os.getenv("S2_NAME") or "S2TEST"
local TABLE = os.getenv("S2_TABLE") or "AI ROOM - 2"
local HANDS = tonumber(os.getenv("S2_HANDS") or "2")
local LIMIT = tonumber(os.getenv("S2_LIMIT") or "840")
local GRID = { "ABCDEFGHIJKLMNOP", "QRSTUVWXYZ012345", "6789-_." }

local phase = "boot"
local pending        -- a press in flight: wait for the screen to change
local hold = 0       -- earliest time for the next action
local snapped = {}
local swaps0
local hand = 0       -- hands seen starting (R1 after something else)
local lastTok = ""
local moves = {}     -- hand index -> moves made in it
local nmoves = 0
local playedHands = {}
local chipsDone = false
local doneAt         -- hand index after which we quit
local lastScreen = ""

local function log(fmt, ...)
  print(string.format("[%7.2f] " .. fmt, T.now(), ...))
end

local function snap(tag)
  if snapped[tag] then return end
  snapped[tag] = true
  T.snap()
  log("SNAP %s\n%s", tag, T.screen())
end

-- the characters and the inverse cells: a cursor move changes only the latter
local function sig()
  local inv = {}
  for y = 0, T.rows - 1 do
    for x = 0, T.cols - 1 do
      if T.inv(y, x) then inv[#inv + 1] = y * 16 + x end
    end
  end
  return T.screen() .. "|" .. table.concat(inv, ",")
end

local function press(key, why)
  log("press %d (%s)", key, why or "")
  T.press("A", key, 8)
  pending = { scr = sig(), t = T.now() + 4 }
end

-- the grid cell holding the cursor (rows 5-7 of the editor)
local function cursor()
  for y = 5, 7 do
    for x = 0, 15 do
      if T.inv(y, x) then return y - 5, x end
    end
  end
end

local function edit()
  local typed = T.row(3):gsub("^%s+", ""):gsub("_$", "")
  if typed ~= NAME:sub(1, #typed) then return press(1, "delete") end
  if typed == NAME then snap("name"); return press(3, "done") end
  local c = NAME:sub(#typed + 1, #typed + 1)
  local ty, tx
  for y, s in ipairs(GRID) do
    local x = s:find(c, 1, true)
    if x then ty, tx = y - 1, x - 1 end
  end
  assert(ty, "name character not on the grid: " .. c)
  local cy, cx = cursor()
  if not cy then return end
  if cy == ty and cx == tx then return press(5, "type " .. c) end
  if cy ~= ty then return press(cy < ty and 8 or 2, "row") end
  local right = (tx - cx) % 16
  return press(right <= 8 and 6 or 4, "column")
end

-- our turn: rows 8-9 hold [n]NAME slots, the digit in inverse
local function menu()
  local slots = {}
  for k = 0, 3 do
    local y, x = 8 + (k // 2), (k % 2) * 8
    if T.inv(y, x) then
      local name = T.row(y):sub(x + 2, x + 8):gsub("%s+$", "")
      slots[#slots + 1] = { key = k + 1, name = name }
    end
  end
  return slots
end

local function choose(slots)
  for _, want in ipairs({ "CHECK", "CALL", "POST2" }) do
    for _, s in ipairs(slots) do
      if s.name == want then return s end
    end
  end
  for _, s in ipairs(slots) do
    if s.name ~= "FOLD" then return s end
  end
  return slots[1]
end

local function count(t)
  local n = 0
  for _ in pairs(t) do n = n + 1 end
  return n
end

local function game()
  if T.find("NET ERROR", 8, 9) then log("net error shown: %s", T.row(9)) end
  local slots = T.char(8, 0) == 0x31 and T.inv(8, 0) and menu() or {}
  if #slots > 0 then
    snap("turn")
    local s = choose(slots)
    moves[hand] = (moves[hand] or 0) + 1
    nmoves = nmoves + 1
    playedHands[hand] = true
    log("hand %d: our turn, %d moves offered, choosing %s", hand,
        #slots, s.name)
    return press(s.key, s.name)
  end
  if T.row(8):find("^POT") then
    local tok = T.row(8):sub(11, 13)
    if tok:sub(1, 2) == "R1" and lastTok:sub(1, 2) ~= "R1" then
      hand = hand + 1
      log("hand %d starts\n%s", hand, T.screen())
      snap("hand" .. hand)
    elseif tok == "END" and lastTok ~= "END" then
      log("hand %d showdown\n%s", hand, T.screen())
      snap("showdown")
    end
    lastTok = tok
  end
  if T.find("WON", 9, 9) or T.find("WINS", 9, 9) then snap("result") end
  if nmoves > 0 and not chipsDone and T.row(9):find("9 CHIPS") then
    chipsDone = "on"
    return press(9, "chips view")
  end
  if chipsDone == "on" and T.row(9):find("9 CARDS") then
    snap("chips")
    chipsDone = true
    return press(9, "cards view")
  end
  -- quit once HANDS hands we played in have finished
  if not doneAt and count(playedHands) >= HANDS then
    local last = 0
    for h in pairs(playedHands) do last = math.max(last, h) end
    doneAt = last
    log("played in %d hands; quitting when hand %d is over", HANDS, last)
  end
  if doneAt and hand > doneAt then
    phase = "quit"
    return press(0, "menu")
  end
end

T.every_frame(function()
  local t = T.now()
  if t > LIMIT then
    T.verdict(false, string.format("%s: out of time (hand %d, %d moves)\n%s",
        phase, hand, nmoves, T.screen()))
    return
  end
  local scr = sig()
  if scr ~= lastScreen then          -- act only on a screen that has settled
    lastScreen = scr
    hold = math.max(hold, t + 0.25)
  end
  if pending then
    if scr ~= pending.scr or t > pending.t then
      if scr == pending.scr then log("no change after the press") end
      pending = nil
      hold = t + 0.3
    end
    return
  end
  if t < hold then return end
  scr = T.screen()
  swaps0 = swaps0 or T.output("fujinet_swaps")

  if phase == "quit" then
    if T.find("4 QUIT TO CONFIG") then
      snap("menu")
      phase = "config"
      return press(4, "quit to CONFIG")
    end
  elseif phase == "config" then
    local sw = T.output("fujinet_swaps")
    if sw > swaps0 then
      log("hand-over: fujinet_swaps %d -> %d, mode %d", swaps0, sw, T.status(0x12))
      T.verdict(true, string.format("name %s, table %s, %d hands played (%d moves), back to CONFIG",
          NAME, TABLE, count(playedHands), nmoves))
    end
  elseif T.find("YOUR NAME") then
    snap("editor")
    phase = "name"
    edit()
  elseif T.find("1-8 JOIN") then
    snap("lobby")
    local r = T.find(TABLE, 1, 8)
    if not r then
      T.verdict(false, "no table " .. TABLE .. " in the lobby:\n" .. scr)
      return
    end
    phase = "game"
    press(r, "join " .. T.row(r))
  elseif phase == "game" and not T.find("JOINING") then
    game()
  end
end)
