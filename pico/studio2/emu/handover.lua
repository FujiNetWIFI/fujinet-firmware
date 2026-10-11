-- handover.lua -- Tier B: the machine as the game first finds it. When the
-- BIOS's interpreter first reads the cart's $0400 (P = 4, R5 = $0400 as the
-- tap runs, before LDA R5 bumps it:
-- the boot byte-code's jump to the cart) after the image is live -- a
-- power-on into the image (DIRECT), or fujiboot's hand-over -- write the CPU's
-- registers, flags and the console's 512 bytes of RAM to $TIERB_OUT, one
-- "name value" per line, and exit. tools/tierb.py compares the two.
local out = os.getenv("TIERB_OUT")
local cpu = manager.machine.devices[":ic1"]
local space = cpu.spaces["program"]
local ram = manager.machine.memory.shares[":ram"]
local direct = os.getenv("FUJINET_IMAGE") ~= nil

local function dump()
  local f = io.open(out, "w")
  local s = cpu.state
  for _, n in ipairs({ "R0", "R1", "R2", "R3", "R4", "R5", "R6", "R7", "R8", "R9",
                       "R10", "R11", "R12", "R13", "R14", "R15", "X", "P", "IE", "Q", "D", "DF", "T" }) do
    f:write(string.format("%s %X\n", n, s[n].value))
  end
  for i = 0, 511 do f:write(string.format("RAM%03X %02X\n", i, ram:read_u8(i))) end
  f:write(string.format("TIME %.6f\n", manager.machine.time:as_double()))
  f:close()
end

_G.__tierb_done = false
-- Not the first read of all: a display switched on mid-frame DMAs from
-- wherever R0 was left, and can walk through $0400 first. Installed from the
-- first frame: the device's own handler, installed at reset, would remove a
-- tap set any earlier. The BIOS reaches the cart about three frames in.
local function tap(offset, data, mask)
  if _G.__tierb_done then return end
  if cpu.state.P.value ~= 4 or cpu.state.R5.value ~= 0x0400 then return end
  if not direct and manager.machine.output:get_value("fujinet_swaps") < 1 then return end
  _G.__tierb_done = true
  dump()
  manager.machine:exit()
end

-- MAME never runs the 1802's reset at power-on (only CLEAR does, and IE
-- stays 0 until then); the console's RC reset holds CLEAR low at power-on.
-- So the native reference pulses CLEAR first, as the hardware would.
_G.__tierb_frames = 0
_G.__tierb_sub = emu.add_machine_frame_notifier(function()
  _G.__tierb_frames = _G.__tierb_frames + 1
  local clear = manager.machine.ioport.ports[":CLEAR"].fields["Clear"]
  if direct and _G.__tierb_frames == 1 then clear:set_value(1) end
  if direct and _G.__tierb_frames == 2 then clear:clear_value() end
  if not _G.__tierb_tap then
    _G.__tierb_tap = space:install_read_tap(0x0400, 0x0400, "tierb", tap)
  end
end)
