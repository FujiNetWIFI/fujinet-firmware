-- sigtest.lua -- the console's own BIOS accepted the cart.
--
-- MAME does not emulate the NTSC BIOS's 2600-mode fallback, so an unsigned
-- cart still boots there; the verdict has to come from which exit the BIOS
-- code takes. NTSC: its RAM copy of the check jumps to $26B9 (pass) or $26C2
-- (2600 mode). PAL has no hash: the pass is reaching the cart's reset code.
-- Either way the loader at $0600 must then run.
local T = dofile(os.getenv("A78_EMU_DIR") .. "/a78text.lua")
local cpu = manager.machine.devices[":maincpu"]
local pal = manager.machine.system.name == "a7800p"
local passed, failed, loader = false, false, false

local function pc() return cpu.state["PC"].value end

_G.__a78_sigtap = T.space():install_read_tap(0x0600, 0x27ff, "sigtest", function(offset, data, mask)
  local p = pc()
  if p == 0x26B9 and offset == 0x26B9 then passed = true end
  if p == 0x26C2 and offset == 0x26C2 then failed = true end
  if p == 0x0600 and offset == 0x0600 then loader = true end
end)

T.every_frame(function()
  if failed then
    T.verdict(false, "the BIOS refused the cart (2600 mode)")
  elseif loader and (passed or pal) then
    T.verdict(true, pal and "PAL BIOS started the boot block; loader running"
                         or "NTSC BIOS signature check passed; loader running")
  elseif T.now() > 10 then
    T.verdict(false, string.format("nothing after 10 s (pass=%s loader=%s)",
                                   tostring(passed), tostring(loader)))
  end
end)
