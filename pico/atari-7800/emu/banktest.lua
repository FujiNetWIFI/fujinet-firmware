-- banktest.lua -- fujiboot pushes fujibank (a claimed 64K SuperGame app),
-- which runs a transaction from each of three banks, then boots a game.
-- PASS: three "BANK n 00 <ssid>" rows on its screen, then a third load that
-- ends with the cart in game mode.
local T = dofile(os.getenv("A78_EMU_DIR") .. "/a78text.lua")
if _G.__banktest then return end
_G.__banktest = { loads = 0, prev = 0, banks = {} }
local S = _G.__banktest
local function out(name) return manager.machine.devices[":"]:output(name):get() end

T.every_frame(function()
  local mode = out("fujinet_mode")
  if S.prev == 1 and (mode == 2 or mode == 3) then S.loads = S.loads + 1 end
  S.prev = mode
  if S.loads == 2 and mode == 3 then
    for n = 0, 2 do
      local y = T.find(string.format("BANK %02X 00", n))
      if y then S.banks[n] = T.row(y) end
    end
  end
  if S.loads == 3 and mode == 2 then
    local got = (S.banks[0] and 1 or 0) + (S.banks[1] and 1 or 0) + (S.banks[2] and 1 or 0)
    T.verdict(got == 3, string.format("%d of 3 banks answered (%s | %s | %s); the game started",
              got, S.banks[0] or "-", S.banks[1] or "-", S.banks[2] or "-"))
  elseif T.now() > 120 then
    T.verdict(false, "loads " .. S.loads .. " mode " .. mode .. "\n" .. T.screen())
  end
end)
