-- s2screen.lua -- what the Studio II cart shows, for the harnesses. Reads the
-- device's save items -- the text engine's characters, its raster, the
-- arena -- never the address space: a Lua read there is a real bus read, and
-- the cart's hotspots would fire.
local T = {}

T.cols, T.rows = 16, 10

local dev = manager.machine.devices[":fujinet"]
local items = {}
local function item(name)
  if not items[name] then
    local idx = dev.items["0/" .. name]
    assert(idx, "no save item " .. name .. " (is the device active?)")
    items[name] = emu.item(idx)
  end
  return items[name]
end

local function shown(c)
  if c >= 0x20 and c < 0x7F then return string.char(c) end
  if c >= 0x01 and c <= 0x04 then return ({ "S", "H", "D", "C" })[c] end
  if c == 0x0A then return "T" end
  return "#"
end

function T.char(y, x) return item("m_text.chars"):read(y * T.cols + x) end
function T.inv(y, x) return item("m_text.inv"):read(y * T.cols + x) ~= 0 end

function T.row(y)
  local s = {}
  for x = 0, T.cols - 1 do s[#s + 1] = shown(T.char(y, x)) end
  return (table.concat(s):gsub("%s+$", ""))
end

function T.screen()
  local out = {}
  for y = 0, T.rows - 1 do out[#out + 1] = string.format("%2d|%s", y, T.row(y)) end
  return table.concat(out, "\n")
end

-- The first row from r0 to r1 (default all) holding `text`.
function T.find(text, r0, r1)
  for y = r0 or 0, r1 or T.rows - 1 do
    if T.row(y):find(text, 1, true) then return y end
  end
end

-- The row from r0 to r1 in inverse video at column `col`: the selection bar.
function T.bar(r0, r1, col)
  for y = r0 or 0, r1 or T.rows - 1 do
    if T.inv(y, col or 0) then return y end
  end
end

-- Byte i of the raster: line i // 8, bit 7 the leftmost pixel.
function T.raster(i) return item("m_text.raster"):read(i) end

-- The status page: FN_R_* - $400.
function T.status(off) return item("m_arena"):read(0x400 + off) end

-- Bursts of DMA reads shorter than 8: lines the 1861 drew wrong.
function T.short_lines() return item("m_short_lines"):read(0) end

function T.output(name) return manager.machine.output:get_value(name) end

-- manager.machine.time.seconds is the integer field; this is the real time.
function T.now() return manager.machine.time:as_double() end

-- Hold a keypad key (pad "A" or "B", key 0-9) for `frames` frames.
T.held = {}
function T.press(pad, key, frames)
  local f = manager.machine.ioport.ports[":" .. pad].fields[pad .. " " .. key]
  f:set_value(1)
  T.held[#T.held + 1] = { field = f, left = frames or 6 }
end
local function release_tick()
  for i = #T.held, 1, -1 do
    local h = T.held[i]
    h.left = h.left - 1
    if h.left <= 0 then h.field:clear_value(); table.remove(T.held, i) end
  end
end

-- CLEAR is the CPU's reset line on the console, not the machine's reset.
-- (set_value takes the logical state: 1 is pressed, even on an active-low bit.)
function T.clear(frames)
  local f = manager.machine.ioport.ports[":CLEAR"].fields["Clear"]
  f:set_value(1)
  T.held[#T.held + 1] = { field = f, left = frames or 3 }
end

function T.snap() manager.machine.video:snapshot() end

function T.verdict(ok, msg)
  print(string.format("VERDICT %s: %s", ok and "PASS" or "FAIL", msg))
  manager.machine:exit()
end

-- The subscription must outlive the script chunk, or GC drops it.
function T.every_frame(fn)
  _G.__s2_frame_sub = emu.add_machine_frame_notifier(function()
    release_tick()
    fn()
  end)
end

return T
