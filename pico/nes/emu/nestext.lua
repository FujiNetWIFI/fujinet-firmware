-- nestext.lua -- read the screen as text.
--
-- The bring-up clients upload a font whose tile number IS the ASCII code, so
-- a nametable row read straight out of PPU memory is the string on screen.
-- readv_u8 has no side effects, which matters: a plain read_u8 through the
-- CPU space would fire mailbox hotspots.
local M = {}

function M.ppu_space()
  return manager.machine.devices[":ppu"].spaces["videoram"]
end

function M.cpu_space()
  return manager.machine.devices[":maincpu"].spaces["program"]
end

-- row 0-29 of nametable 0, trailing spaces trimmed. Bit 7 is reverse video
-- (cc65's conio and the bring-up font both leave tiles $80+ to it).
function M.row(r)
  local s = M.ppu_space()
  local t = {}
  for c = 0, 31 do
    local v = s:readv_u8(0x2000 + r * 32 + c) & 0x7F
    if v >= 0x20 and v < 0x7F then t[#t + 1] = string.char(v) else t[#t + 1] = " " end
  end
  return (table.concat(t):gsub("%s+$", ""))
end

-- is row r drawn in reverse video (the selection bar)?
function M.inverted(r)
  return M.ppu_space():readv_u8(0x2000 + r * 32 + 1) >= 0x80
end

function M.screen()
  local t = {}
  for r = 0, 29 do t[#t + 1] = M.row(r) end
  return table.concat(t, "\n")
end

function M.find(text)
  for r = 0, 29 do
    if M.row(r):find(text, 1, true) then return r end
  end
  return nil
end

function M.now()
  return manager.machine.time:as_double()
end

-- The frame notifier token must stay referenced or the subscription dies at
-- the next GC (learned on the ColecoVision).
function M.every_frame(fn)
  _G.__nestext_sub = emu.add_machine_frame_notifier(fn)
end

function M.verdict(ok, msg)
  emu.print_info((ok and "PASS: " or "FAIL: ") .. msg)
  print((ok and "PASS: " or "FAIL: ") .. msg)
  manager.machine:exit()
end

return M
