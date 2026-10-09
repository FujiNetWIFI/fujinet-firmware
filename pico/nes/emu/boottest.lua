-- boottest.lua -- M2: fujiboot mounts an image over the network, the loader
-- copies it into the SRAMs and the new image runs.
--   BOOT_EXPECT   text the booted image puts on screen (default "HELLO, WORLD")
--   BOOT_TIMEOUT  seconds (default 90: TNFS + a slice copy per KB)
-- With FUJINET_BOOTDUMP set, the device also wrote the SRAM contents to
-- <dump>.prg/.chr when the load finished; boottest.sh compares them.
local T = dofile(os.getenv("NES_EMU_DIR") .. "/nestext.lua")
local expect = os.getenv("BOOT_EXPECT") or "HELLO, WORLD"
local timeout = tonumber(os.getenv("BOOT_TIMEOUT") or "90")
local seen_mount = false
local done = false

T.every_frame(function()
  if done then return end
  local now = T.now()
  if not seen_mount and T.find("MOUNTING") then
    seen_mount = true
    print(string.format("boottest: fujiboot is up at %.2fs", now))
  end
  if T.find(expect) then
    done = true
    print(T.screen())
    T.verdict(true, string.format("booted image shows %q at %.2fs", expect, now))
  elseif T.find("FAILED STEP") then
    done = true
    print(T.screen())
    T.verdict(false, "fujiboot reported a failure")
  elseif now > timeout then
    done = true
    print(T.screen())
    T.verdict(false, string.format("%q never appeared", expect))
  end
end)
