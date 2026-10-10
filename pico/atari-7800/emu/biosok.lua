-- biosok.lua -- would this console's BIOS start this image as a 7800 game?
--
-- The image is in the SRAM from power-on (FUJINET_IMAGE). MAME skips the
-- 2600-mode switch, so the BIOS's own decision is read off the bus: the
-- NTSC check's exits at $26B9 (pass) and $26C2 (fail), and, on either
-- console, a first lock of INPTCTRL with MARIA disabled -- 2600 mode.
-- Writes after the first lock reach the TIA only, as on the console.
-- Prints "BIOSOK 1" or "BIOSOK 0".
local T = dofile(os.getenv("A78_EMU_DIR") .. "/a78text.lua")
local cpu = manager.machine.devices[":maincpu"]
local pal = manager.machine.system.name == "a7800p"
local pass, fail, locked7800, locked = false, false, false, false

_G.__a78_ok_rd = T.space():install_read_tap(0x26B9, 0x26C2, "biosok_rd", function(offset)
  local p = cpu.state["PC"].value
  if p == 0x26B9 and offset == 0x26B9 then pass = true end
  if p == 0x26C2 and offset == 0x26C2 then fail = true end
end)
_G.__a78_ok_wr = T.space():install_write_tap(0x0000, 0x03FF, "biosok_wr", function(offset, data)
  if locked or (offset & 0xFCE0) ~= 0 or (data & 1) == 0 then return end
  locked = true
  if (data & 2) == 0 then fail = true else locked7800 = true end
end)

local function out(ok)
  print("BIOSOK " .. (ok and "1" or "0"))
  manager.machine:exit()
end

T.every_frame(function()
  if fail then out(false)
  elseif not pal and pass then out(true)
  elseif pal and locked7800 then out(true)
  elseif T.now() > 8 then out(pal)   -- PAL: no hash, and no 2600 switch seen
  end
end)
