-- ramtap.lua -- does a SuperGame RAM title touch $6000-$7FFF? The 8K boards
-- (Impossible Mission, Jinks, Tower Toppler) carry one 6264, which the cart
-- shows twice in $4000-$7FFF; MAME indexes past an 8K buffer, so a title that
-- reaches the top half differs between them.
local T = dofile(os.getenv("A78_EMU_DIR") .. "/a78text.lua")
local n = 0
local ports = manager.machine.ioport.ports
_G.__rt_r = T.space():install_read_tap(0x6000, 0x7fff, "rt_r", function() n = n + 1 end)
_G.__rt_w = T.space():install_write_tap(0x6000, 0x7fff, "rt_w", function() n = n + 1 end)
local frame = 0
_G.__rt_f = emu.add_machine_frame_notifier(function()
  frame = frame + 1
  local f = frame - 300
  local b = ports[":BUTTONS"].fields["P1 Button 1"]
  b:set_value((f >= 0 and f % 120 < 6) and 1 or 0)
  if frame > 3600 then
    print("UPPER8K " .. n)
    manager.machine:exit()
  end
end)
