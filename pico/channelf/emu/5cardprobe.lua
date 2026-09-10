local VSTATE = 0x8006
local sp, n, timer, i, phase = nil, 0, 0, 1, "wait"
local seq = {
    "P1 Push Down","P1 Push Down","P1 Push Down",
    "P1 Down","P1 Down","P1 Down","P1 Down",
    "HOLD (Button 2)","HOLD (Button 2)","HOLD (Button 2)",
    "P1 Push Down",
}
local function port(f) return f:match("^P1") and ":RIGHT_C" or ":PANEL" end
local function txt(a, n2)
    local s = ""
    for k = 0, n2 - 1 do
        local c = sp:readv_u8(a + k)
        s = s .. (c >= 32 and c < 127 and string.char(c) or string.format("<%02X>", c))
    end
    return s
end
_G._p = emu.add_machine_frame_notifier(function()
    n = n + 1
    sp = sp or manager.machine.devices[":maincpu"].spaces["program"]
    timer = timer + 1
    local st = sp:readv_u8(VSTATE)
    if phase == "wait" then
        if st == 1 and timer > 240 then phase, timer = "keys", 0
        elseif st ~= 1 then timer = 0 end
        if n > 20000 then print("P: no keyboard"); manager.machine:exit() end
    elseif phase == "keys" then
        if i > #seq then phase, timer = "lobby", 0
        elseif timer == 1 then manager.machine.ioport.ports[port(seq[i])].fields[seq[i]]:set_value(1)
        elseif timer == 10 then manager.machine.ioport.ports[port(seq[i])].fields[seq[i]]:set_value(0)
        elseif timer >= 40 then i = i + 1; timer = 0 end
    elseif phase == "lobby" then
        if timer % 600 == 0 then
            print(string.format("P: settle AV=%02X%02X PRV=%02X%02X MIN=%02X%02X TRY=%02X ST=%02X",
                sp:readv_u8(0x8014), sp:readv_u8(0x8013),
                sp:readv_u8(0x8016), sp:readv_u8(0x8015),
                sp:readv_u8(0x8027), sp:readv_u8(0x8026),
                sp:readv_u8(0x8017), st))
        end
        if st == 2 and timer > 900 then
            print("P: VNAME  = " .. txt(0x8060, 10))
            phase, timer = "sit", 0
        elseif timer > 20000 then print("P: no lobby"); manager.machine:exit() end
    elseif phase == "sit" then
        if timer == 30 then manager.machine.ioport.ports[":RIGHT_C"].fields["P1 Push Down"]:set_value(1)
        elseif timer == 40 then manager.machine.ioport.ports[":RIGHT_C"].fields["P1 Push Down"]:set_value(0)
        elseif timer > 40 then
            print("P: VTABLE = " .. txt(0x8050, 12))
            phase, timer = "game", 0
        end
    elseif phase == "game" then
        if timer == 2400 then
            print(string.format("P: rxlen=%02X%02X", sp:readv_u8(0x8019), sp:readv_u8(0x8018)))
            print("P: VLINE  = " .. txt(0x8100, 26))
            print("P: VDIGIT = " .. txt(0x8140, 8))
            print(string.format("P: DEC5 V=%02X%02X P=%02X%02X DIG=%02X IDX=%02X LEAD=%02X",
                sp:readv_u8(0x8151), sp:readv_u8(0x8150),
                sp:readv_u8(0x8153), sp:readv_u8(0x8152),
                sp:readv_u8(0x8154), sp:readv_u8(0x8155), sp:readv_u8(0x8156)))
            local h = ""
            for k = 154, 190 do h = h .. string.format("%02X ", sp:readv_u8(0xF800 + k)) end
            print("P: rec0   = " .. h)
            local len = sp:readv_u8(0x8018) + sp:readv_u8(0x8019) * 256
            for base = 0, len - 1, 32 do
                local h, t = "", ""
                for k = base, math.min(base + 31, len - 1) do
                    local c = sp:readv_u8(0xF800 + k)
                    h = h .. string.format("%02X", c)
                    t = t .. (c >= 32 and c < 127 and string.char(c) or ".")
                end
                print(string.format("P: %03d %-64s %s", base, h, t))
            end
            manager.machine:exit()
        end
    end
end)
