-- fttest.lua -- M1: fujitest's live transaction. SSID, IP and firmware come
-- off a socket to fujinet-pc through the cart's own fujimail.c and are
-- printed by the cart's text engine from its reply window.
local T = dofile(os.getenv("S2_EMU_DIR") .. "/s2screen.lua")

T.every_frame(function()
  local t = T.now()
  local seq = T.find("SEQ ")
  if seq and t > 1 then
    print(T.screen())
    T.snap()
    local ok = T.row(seq):find("SEQ 01", 1, true) and T.row(seq):find("ERR 00", 1, true)
        and T.row(4) ~= "" and T.row(6) ~= ""
    T.verdict(ok, ok and ("SSID/IP/FW live: " .. T.row(2) .. " / " .. T.row(4) .. " / " .. T.row(6))
                     or "the transaction failed")
  elseif t > 30 then
    T.verdict(false, "fujitest never finished:\n" .. T.screen())
  end
end)
