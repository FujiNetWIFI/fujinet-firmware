-- pokeylog.lua -- every write to the cart's POKEY at $4000 (Ballblazer,
-- Commando), as "PK clock register value" lines for tools/pokeyrender.c,
-- for POKEYLOG_SECS seconds of the game's attract mode.
local T = dofile(os.getenv("A78_EMU_DIR") .. "/a78text.lua")
local secs = tonumber(os.getenv("POKEYLOG_SECS") or "30")
local clock = 1789773
_G.__pk = T.space():install_write_tap(0x4000, 0x7fff, "pokeylog", function(offset, data)
  local t = manager.machine.time:as_double()
  print(string.format("PK %d %x %x", math.floor(t * clock), offset & 0x0f, data))
end)
T.every_frame(function()
  if T.now() > secs then manager.machine:exit() end
end)
