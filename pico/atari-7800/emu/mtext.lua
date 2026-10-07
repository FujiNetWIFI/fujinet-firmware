-- mtext.lua -- the screen of a client on the shared MARIA engine (CONFIG,
-- fujinet-config atari7800/maria.s) as text, for the harnesses. Its 32 x 24
-- map sits at the start of the cart's RAM, a byte per cell holding tile * 2;
-- the palette selects follow 3 pages on. Tiles below $20 are frame and bar
-- pieces, drawn here as ASCII. MT_MAP moves the map for a client whose
-- linker config puts it elsewhere.
local M = {}

local COLS, ROWS = 32, 24
local MAP = tonumber(os.getenv("MT_MAP") or "0x4000")
local ATTR = MAP + 0x300
M.cols, M.rows = COLS, ROWS

local PIECES = { [0x01] = "-", [0x02] = "|", [0x03] = "+", [0x04] = "+",
                 [0x05] = "+", [0x06] = "+", [0x07] = "#" }

local function space()
  return manager.machine.devices[":maincpu"].spaces["program"]
end
M.space = space

function M.tile(x, y)
  return space():read_u8(MAP + y * COLS + x) >> 1
end

function M.hi(x, y)
  return space():read_u8(ATTR + y * COLS + x) ~= 0
end

function M.row(y)
  local s = {}
  for x = 0, COLS - 1 do
    local t = M.tile(x, y)
    if t >= 32 and t < 127 then
      s[#s + 1] = string.char(t)
    elseif t >= 0x10 and t <= 0x18 then
      s[#s + 1] = t == 0x10 and "." or "="
    else
      s[#s + 1] = PIECES[t] or " "
    end
  end
  return (table.concat(s):gsub("%s+$", ""))
end

-- The selection bar: the first row whose text area is in palette 1.
function M.bar()
  for y = 0, ROWS - 1 do
    if M.hi(2, y) and M.hi(29, y) then return y end
  end
end

-- Rows marked '>' are on the bar.
function M.screen()
  local out, bar = {}, M.bar()
  for y = 0, ROWS - 1 do
    out[#out + 1] = string.format("%s%2d %s", y == bar and ">" or " ", y, M.row(y))
  end
  return table.concat(out, "\n")
end

function M.find(text, from, to)
  for y = from or 0, to or ROWS - 1 do
    if M.row(y):find(text, 1, true) then return y end
  end
end

function M.now()
  return manager.machine.time:as_double()
end

-- The cart's status page ($0C00 + FN_R_* - $400); gone once a game runs.
function M.status(off)
  return space():read_u8(0x0C00 + off)
end

function M.verdict(ok, msg)
  print(string.format("VERDICT %s: %s", ok and "PASS" or "FAIL", msg))
  manager.machine:exit()
end

-- The subscription must outlive the script chunk, or GC drops it.
function M.every_frame(fn)
  _G.__mtext_frame_sub = emu.add_machine_frame_notifier(fn)
end

return M
