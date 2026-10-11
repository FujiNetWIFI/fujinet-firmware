-- ramdump.lua -- after two seconds write the console's 512 bytes of RAM to $RAMOUT
-- (hex, space-separated) and exit: tools/abrun.py reads which pages the
-- synthetic corpus probe copied.
_G.n = 0
_G.__s = emu.add_machine_frame_notifier(function()
  _G.n = _G.n + 1
  if _G.n == 120 then
    local ram = manager.machine.memory.shares[":ram"]
    local out = {}
    for i = 0, 511 do out[#out + 1] = string.format("%02X", ram:read_u8(i)) end
    local f = io.open(os.getenv("RAMOUT"), "w"); f:write(table.concat(out, " ")); f:close()
    manager.machine:exit()
  end
end)
