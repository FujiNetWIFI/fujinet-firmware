-- handover.lua -- Tier B: the machine as the BIOS finds it when it reads
-- $BFFD, its first look at the cart. HO_MODE=native takes the first such read
-- after power-on (the image served from the start); HO_MODE=net the first
-- after the cart's swap (fujiboot's hand-over). Writes RAM $0000-$3FFF to
-- HO_OUT and prints the CPU's registers, then exits.
local T = dofile(os.getenv("A52_EMU_DIR") .. "/a52text.lua")
local mode = os.getenv("HO_MODE") or "native"
local out = os.getenv("HO_OUT")

_G.ho = _G.ho or { done = false }
local space = T.space()
local cpu = manager.machine.devices[":maincpu"]

local function reg(name)
  local e = cpu.state[name]
  return e and e.value or -1
end

_G.ho_tap = space:install_read_tap(0xBFFD, 0xBFFD, "handover", function(offset, data, mask)
  if _G.ho.done then return end
  if mode == "net" and manager.machine.output:get_value("fujinet_swaps") < 1 then return end
  _G.ho.done = true
  local f = io.open(out, "wb")
  for a = 0, 0x3FFF do f:write(string.char(space:read_u8(a))) end
  f:close()
  print(string.format("HO %s SP=%02X X=%02X P=%02X", mode, reg("SP"), reg("X"), reg("P")))
  manager.machine:exit()
end)
