-- M2 gate: mount an image over the network, boot it, and prove the served
-- window is byte-identical to the file that was pushed.
--
-- The comparison is against the file on disk, not against anything the cart
-- computed, so a mapper that quietly mangled the image would fail here.
-- CHECK_AT must be late enough for the mount, the DBC push and the swap.
local WANT = os.getenv("BOOT_IMAGE") or "build/hello.bin"
local AT = tonumber(os.getenv("CHECK_AT") or "900")
local n = 0

_G._bt = emu.add_machine_frame_notifier(function()
    n = n + 1
    if n ~= AT then return end

    local f = io.open(WANT, "rb")
    if not f then
        print("BOOTTEST: FAIL -- cannot open " .. WANT)
        manager.machine:exit()
        return
    end
    local img = f:read("*a")
    f:close()

    local sp = manager.machine.devices[":maincpu"].spaces["program"]
    local bad, first = 0, nil
    for i = 1, #img do
        -- readv_u8, never read_u8: the plain form fires read handlers
        local got = sp:readv_u8(0x0800 + i - 1)
        if got ~= img:byte(i) then
            bad = bad + 1
            if not first then
                first = string.format("$%04X: served $%02X, file $%02X",
                                      0x0800 + i - 1, got, img:byte(i))
            end
        end
    end

    -- Past the image the window must read $FF, exactly as MAME's own stock
    -- Videocart device answers beyond its ROM size.
    local fill = 0
    for a = 0x0800 + #img, 0x47FF do
        if sp:readv_u8(a) ~= 0xFF then fill = fill + 1 end
    end

    print(string.format("BOOTTEST: %s: %d bytes, %d mismatched, %d non-$FF fill bytes",
                        WANT, #img, bad, fill))
    if first then print("BOOTTEST: first " .. first) end
    if bad == 0 and fill == 0 then
        print("BOOTTEST: PASS -- the booted image is byte-identical to the file")
    else
        print("BOOTTEST: FAIL")
    end
    manager.machine:exit()
end)
