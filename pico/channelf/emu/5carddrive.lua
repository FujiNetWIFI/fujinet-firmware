-- Drive 5 Card Stud: type a name on the on-screen keyboard, commit it, and
-- reach the lobby. Waits on the program's own state, never on frame numbers.
local ST_EDIT, ST_LOBBY, ST_GAME = 1, 2, 3
local VSTATE = 0x8006

local seq = {
    -- three letters, then down to the control row, right to OK, fire
    "P1 Push Down", "P1 Push Down", "P1 Push Down",
    "P1 Down", "P1 Down", "P1 Down", "P1 Down",
    "HOLD (Button 2)", "HOLD (Button 2)", "HOLD (Button 2)",
    "P1 Push Down",
}
local sp, n, timer, i, phase = nil, 0, 0, 1, "wait"

local function port(f) return f:match("^P1") and ":RIGHT_C" or ":PANEL" end

_G._5d = emu.add_machine_frame_notifier(function()
    n = n + 1
    sp = sp or manager.machine.devices[":maincpu"].spaces["program"]
    timer = timer + 1
    local st = sp:readv_u8(VSTATE)

    if phase == "wait" then
        if st == ST_EDIT and timer > 240 then
            print("5CARD: keyboard up, typing a name")
            phase, timer = "keys", 0
        elseif st ~= ST_EDIT then timer = 0 end
        if n > 20000 then print("5CARD: FAIL -- no keyboard"); manager.machine:exit() end

    elseif phase == "keys" then
        if i > #seq then
            print("5CARD: name committed, waiting for the lobby")
            phase, timer = "lobby", 0
        elseif timer == 1 then
            manager.machine.ioport.ports[port(seq[i])].fields[seq[i]]:set_value(1)
        elseif timer == 10 then
            manager.machine.ioport.ports[port(seq[i])].fields[seq[i]]:set_value(0)
        elseif timer >= 40 then i = i + 1; timer = 0 end

    elseif phase == "lobby" then
        if st == ST_LOBBY and timer > 900 then
            print(string.format("5CARD: at the lobby, %d tables", sp:readv_u8(0x801A)))
            manager.machine.video:snapshot()
            phase, timer = "sit", 0
        elseif timer > 20000 then
            print(string.format("5CARD: FAIL -- state is %02X", st))
            manager.machine.video:snapshot()
            manager.machine:exit()
        end
    elseif phase == "sit" then
        if timer == 30 then
            manager.machine.ioport.ports[":RIGHT_C"].fields["P1 Push Down"]:set_value(1)
        elseif timer == 40 then
            manager.machine.ioport.ports[":RIGHT_C"].fields["P1 Push Down"]:set_value(0)
        elseif timer > 40 then
            print("5CARD: sitting down")
            phase, timer = "game", 0
        end

    elseif phase == "game" then
        if st == ST_GAME and timer > 2400 then
            print("5CARD: at the table")
            manager.machine.video:snapshot()
            phase, timer = "done", 0
        elseif timer > 30000 then
            print(string.format("5CARD: FAIL -- state is %02X", st))
            manager.machine.video:snapshot()
            manager.machine:exit()
        end

    elseif phase == "done" then
        if timer > 30 then manager.machine:exit() end
    end
end)
