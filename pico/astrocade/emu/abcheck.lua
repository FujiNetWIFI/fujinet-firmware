-- abcheck.lua: launch the first menu entry and watch the self-test verdict.
--
-- The banked test clients (fujibank.asm, gamebank.asm) poke 4FA0H with A5H
-- on pass, 5AH on fail (failing page/bank in 4FA1H). This presses '1' at
-- emulated t=3 the way drive.lua does, then samples the verdict each frame
-- (screen RAM only -- no cart handler can fire) and exits, printing one
-- machine-readable line:  abcheck: PASS | FAIL page=NN | TIMEOUT
--
--   mame astrocde ... -autoboot_script emu/abcheck.lua -video none -sound none
--   ABCHECK_TIMEOUT   emulated-seconds budget (default 90)
--   ABCHECK_PRESSES   comma list of press times (default "3"; a network
--                     boot needs "3,25": launch the client, then launch the
--                     pushed image's own menu entry after the swap)

local timeout = tonumber(os.getenv("ABCHECK_TIMEOUT") or "90")
local presses = {}
for n in string.gmatch(os.getenv("ABCHECK_PRESSES") or "3", "[^,]+") do
    presses[#presses + 1] = tonumber(n)
end
local state = 0
local down = false
local finished = false

emu.register_frame(function()
    if finished then return end
    local t = manager.machine.time.seconds
    local port = manager.machine.ioport.ports[":KEYPAD3"]
    if port == nil then return end

    if not down and state < #presses and t >= presses[state + 1] then
        port:field(0x10):set_value(1)
        down = true
    elseif down and t >= presses[state + 1] + 2 then
        port:field(0x10):clear_value()
        down = false
        state = state + 1
    end

    if state < #presses then return end -- power-on RAM could fake a verdict
    local mem = manager.machine.devices[":maincpu"].spaces["program"]
    local v = mem:read_u8(0x4FA0)
    if v == 0xA5 then
        print("abcheck: PASS")
        finished = true
        manager.machine:exit()
    elseif v == 0x5A then
        print(string.format("abcheck: FAIL page=%02X", mem:read_u8(0x4FA1)))
        finished = true
        manager.machine:exit()
    elseif t >= timeout then
        print("abcheck: TIMEOUT")
        finished = true
        manager.machine:exit()
    end
end)
