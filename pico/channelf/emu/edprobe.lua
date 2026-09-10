-- Walk to the host list, press MODE (rename) and watch the editor.
local VSTATE = 0x8006
local sp, n, timer, i, phase = nil, 0, 0, 1, "wait"
local seq = {"MODE (Button 3)"}
_G._e = emu.add_machine_frame_notifier(function()
    n = n + 1
    sp = sp or manager.machine.devices[":maincpu"].spaces["program"]
    timer = timer + 1
    if phase == "wait" then
        if sp:readv_u8(VSTATE) == 4 then
            if timer > 300 then
                print("EDP: at the host list; pressing MODE")
                phase, timer = "keys", 0
            end
        else timer = 0 end
        if n > 30000 then print("EDP: FAIL -- never reached the host list"); manager.machine:exit() end
    elseif phase == "keys" then
        if i > #seq then phase, timer = "watch", 0
        elseif timer == 1 then manager.machine.ioport.ports[":PANEL"].fields[seq[i]]:set_value(1)
        elseif timer == 8 then manager.machine.ioport.ports[":PANEL"].fields[seq[i]]:set_value(0)
        elseif timer >= 20 then i = i + 1; timer = 0 end
    elseif phase == "watch" then
        if timer == 400 or timer == 1500 then
            local cpu = manager.machine.devices[":maincpu"]
            print(string.format("EDP: t=%d PC0=%04X VSTATE=%02X VEDX=%02X VEDY=%02X VEDLEN=%02X VROW=%02X",
                timer, cpu.state["PC0"].value, sp:readv_u8(VSTATE),
                sp:readv_u8(0x8013), sp:readv_u8(0x8014), sp:readv_u8(0x8015), sp:readv_u8(0x800D)))
        elseif timer == 1600 then
            manager.machine.video:snapshot()
        elseif timer > 1650 then manager.machine:exit() end
    end
end)
