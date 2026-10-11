-- s2trace.lua -- every bus read of a client's run, for host_test/test_cycles.
-- One line per read to $S2TRACE: "F addr op x p rn rx" for an opcode fetch
-- (with the registers its execute cycle uses), "M addr" for display DMA,
-- "D addr" for any other read. Stops when the client's screen shows
-- $S2TRACE_UNTIL (default "SEQ "; "SWAP": once the cart has swapped an image
-- in) or after $S2TRACE_MAX reads.
local T = dofile(os.getenv("S2_EMU_DIR") .. "/s2screen.lua")
local cpu = manager.machine.devices[":ic1"]
local space = cpu.spaces["program"]
local until_text = os.getenv("S2TRACE_UNTIL") or "SEQ "
local max = tonumber(os.getenv("S2TRACE_MAX") or "2000000")
local out = assert(io.open(os.getenv("S2TRACE"), "w"))
local n = 0
local st = cpu.state
local R = {}
for i = 0, 15 do R[i] = st["R" .. i] end

local function tap(offset, data, mask)
  n = n + 1
  local p = st.P.value
  local nxt = (offset + 1) & 0xFFFF
  if R[p].value == nxt then                                -- MAME bumps R(P) first
    local x = st.X.value
    out:write(string.format("F %04X %02X %X %X %04X %04X\n", offset, data, x, p,
                            R[data & 15].value, R[x].value))
  elseif R[0].value == nxt then                            -- and R0 for DMA
    out:write(string.format("M %04X\n", offset))
  else
    out:write(string.format("D %04X\n", offset))
  end
  if n >= max then out:close(); manager.machine:exit() end
end

-- Installed from the first frame: the device's handler, installed at reset,
-- would remove a tap set any earlier.
_G.__tr_sub = emu.add_machine_frame_notifier(function()
  if not _G.__tr_tap then
    _G.__tr_tap = space:install_read_tap(0x0000, 0xFFFF, "s2trace", tap)
  elseif (until_text == "SWAP" and T.output("fujinet_swaps") >= 1)
      or (until_text ~= "SWAP" and T.find(until_text)) then
    out:close()
    manager.machine:exit()
  end
end)
