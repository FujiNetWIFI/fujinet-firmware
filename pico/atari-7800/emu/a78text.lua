-- a78text.lua -- the 7800 screen as text, for the harnesses. Reads the
-- bring-up clients' 40 x 24 character buffer at $4000 (the cart's RAM,
-- testrom/fujidisp.s) through the program space. Codes $80+ are reverse
-- video of the same character.
local T = {}

local COLS, ROWS, SCR = 40, 24, 0x4000
T.cols, T.rows, T.base = COLS, ROWS, SCR

local function space()
  return manager.machine.devices[":maincpu"].spaces["program"]
end
T.space = space

function T.row(y)
  local s = {}
  for x = 0, T.cols - 1 do
    local c = space():read_u8(T.base + y * T.cols + x) & 0x7F
    s[#s + 1] = (c >= 32 and c < 127) and string.char(c) or " "
  end
  return (table.concat(s):gsub("%s+$", ""))
end

function T.screen()
  local out = {}
  for y = 0, T.rows - 1 do out[#out + 1] = T.row(y) end
  return table.concat(out, "\n")
end

function T.find(text)
  for y = 0, T.rows - 1 do
    if T.row(y):find(text, 1, true) then return y end
  end
end

function T.now()
  return manager.machine.time.seconds
end

-- The cart's status page ($0C00 + FN_R_* - $400).
function T.status(off)
  return space():read_u8(0x0C00 + off)
end

function T.verdict(ok, msg)
  print(string.format("VERDICT %s: %s", ok and "PASS" or "FAIL", msg))
  manager.machine:exit()
end

-- The subscription must outlive the script chunk, or GC drops it.
function T.every_frame(fn)
  _G.__a78_frame_sub = emu.add_machine_frame_notifier(fn)
end

return T
