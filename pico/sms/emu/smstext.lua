-- smstext.lua -- the SMS screen as text, for the harnesses. Reads the Mode 4
-- name table at $3800 out of the VDP's own VRAM. The low 7 bits of a cell's
-- tile are its character: testrom/fujidisp.c loads one font at tile 32,
-- fujinet-config one per colour at 32, 160 and 288.
local T = {}

local function vdp()
  for tag, dev in pairs(manager.machine.devices) do
    if tag:match("vdp$") and dev.spaces["videoram"] then return dev end
  end
end

function T.row(y)
  local v = vdp().spaces["videoram"]
  local s = {}
  for x = 0, 31 do
    local c = v:read_u8(0x3800 + (y * 32 + x) * 2) & 0x7F
    s[#s + 1] = (c >= 32 and c < 127) and string.char(c) or " "
  end
  return (table.concat(s):gsub("%s+$", ""))
end

-- Is the cell highlighted (drawn from the sprite palette)?
function T.hi(y, x)
  return (vdp().spaces["videoram"]:read_u8(0x3800 + (y * 32 + (x or 2)) * 2 + 1) & 0x08) ~= 0
end

function T.screen()
  local out = {}
  for y = 0, 23 do out[#out + 1] = T.row(y) end
  return table.concat(out, "\n")
end

function T.find(text)
  for y = 0, 23 do
    if T.row(y):find(text, 1, true) then return y end
  end
end

function T.now()
  return manager.machine.time.seconds
end

function T.verdict(ok, msg)
  print(string.format("VERDICT %s: %s", ok and "PASS" or "FAIL", msg))
  manager.machine:exit()
end

-- The subscription must outlive the script chunk, or GC drops it.
function T.every_frame(fn)
  _G.__sms_frame_sub = emu.add_machine_frame_notifier(fn)
end

return T
