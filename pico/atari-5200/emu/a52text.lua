-- a52text.lua -- the 5200 screen as text, for the harnesses. Reads a 40 x 24
-- mode-2 screen through the program space: the bring-up clients' at $1000
-- (testrom/fujidisp.s), or CONFIG's at $3C00 with A52_SCREEN=0x3C00.
-- Characters are ANTIC internal codes; $80+ is the same in reverse video.
local T = {}

T.cols, T.rows = 40, 24
T.base = tonumber(os.getenv("A52_SCREEN") or "0x1000")
T.status_base = 0xB400

local function space()
  return manager.machine.devices[":maincpu"].spaces["program"]
end
T.space = space

local function ascii(c)
  c = c & 0x7F
  if c < 0x40 then return c + 0x20 end
  if c < 0x60 then return 0x20 end
  return c
end

function T.row(y)
  local s = {}
  for x = 0, T.cols - 1 do
    s[#s + 1] = string.char(ascii(space():read_u8(T.base + y * T.cols + x)))
  end
  return (table.concat(s):gsub("%s+$", ""))
end

function T.screen()
  local out = {}
  for y = 0, T.rows - 1 do out[#out + 1] = T.row(y) end
  return table.concat(out, "\n")
end

-- The first row from r0 to r1 (default all) holding `text`.
function T.find(text, r0, r1)
  for y = r0 or 0, r1 or T.rows - 1 do
    if T.row(y):find(text, 1, true) then return y end
  end
end

-- The row from r0 to r1 in reverse video at column `col`: the selection bar.
function T.bar(r0, r1, col)
  for y = r0 or 0, r1 or T.rows - 1 do
    if space():read_u8(T.base + y * T.cols + (col or 3)) & 0x80 ~= 0 then return y end
  end
end

-- manager.machine.time.seconds is the integer field; this is the real time.
function T.now()
  return manager.machine.time:as_double()
end

-- The cart's status page ($B400 + FN_R_* - $400). Never read the hotspot
-- pages from here: a Lua read is a real bus read.
function T.status(off)
  return space():read_u8(T.status_base + off)
end

function T.verdict(ok, msg)
  print(string.format("VERDICT %s: %s", ok and "PASS" or "FAIL", msg))
  manager.machine:exit()
end

-- The subscription must outlive the script chunk, or GC drops it.
function T.every_frame(fn)
  _G.__a52_frame_sub = emu.add_machine_frame_notifier(fn)
end

return T
