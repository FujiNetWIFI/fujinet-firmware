-- Mount an image over the network, boot it, and prove the served window is
-- byte-identical to the file that was pushed -- and that everything past the
-- image reads $FF, which is what MAME's own stock Videocart device answers
-- past its ROM size.
--
-- The comparison is against the file on disk, not against anything the cart
-- computed, so a mapper that quietly mangled the image would fail here.
--
-- A banked (APPBANK) image is compared page by page: the probe selects each
-- page itself, with a store to $FD80+n, and reads it back through $4800. An
-- image the cart refuses ends with BOOTTEST: REFUSED and the boot error, which
-- tools/soak.sh checks against what the image's name says it expects.
--
-- Polls for the swap rather than waiting a fixed number of frames: a soak run
-- is dominated by that wait otherwise, and a slow socket round trip would turn
-- a fixed deadline into a flake.
local WANT = os.getenv("BOOT_IMAGE") or ""
local DEADLINE = tonumber(os.getenv("BOOT_DEADLINE") or "6000")

local img, sp, n = nil, nil, 0

-- Cheap discriminator, polled until it holds. It must include the byte just
-- PAST the image: every Channel F cart image starts with the same $55 BIOS
-- signature, so a one-byte image compared head-only matches the client that is
-- still running and the swap looks like it already happened.
local FIXED, PAGE = 0x4000, 0x2000

local function looks_swapped()
    if #img > FIXED then
        -- the first and last bytes of the fixed 16K, and page 0's first byte
        for _, a in ipairs({ 0, 1, 2, FIXED - 1, FIXED }) do
            if sp:readv_u8(0x0800 + a) ~= img:byte(a + 1) then return false end
        end
        return true
    end
    if sp:readv_u8(0x0800) ~= img:byte(1) then return false end
    if sp:readv_u8(0x0800 + #img - 1) ~= img:byte(#img) then return false end
    if #img < 0x4000 and sp:readv_u8(0x0800 + #img) ~= 0xFF then return false end
    for i = 1, math.min(16, #img) do
        if sp:readv_u8(0x0800 + i - 1) ~= img:byte(i) then return false end
    end
    return true
end

local function report_banked()
    local bad, first, fill = 0, nil, 0
    local function cmp(addr, off)
        local got = sp:readv_u8(addr)
        if got ~= img:byte(off + 1) then
            bad = bad + 1
            if not first then
                first = string.format("image $%05X at $%04X: served $%02X, file $%02X",
                                      off, addr, got, img:byte(off + 1))
            end
        end
    end
    for off = 0, FIXED - 1 do cmp(0x0800 + off, off) end
    local npages = (#img - FIXED) // PAGE
    for pg = 0, npages - 1 do
        sp:write_u8(0xFD80 + pg, 0)          -- select, as a client would
        for i = 0, PAGE - 1 do cmp(0x4800 + i, FIXED + pg * PAGE + i) end
    end
    sp:write_u8(0xFD80 + npages, 0)          -- one past the end: all $FF
    for a = 0x4800, 0x67FF do
        if sp:readv_u8(a) ~= 0xFF then fill = fill + 1 end
    end
    for a = 0x6800, 0x7FFF do
        if sp:readv_u8(a) ~= 0xFF then fill = fill + 1 end
    end
    sp:write_u8(0xFD80, 0)
    print(string.format("BOOTTEST: %s: %d bytes = 16K + %d pages, %d mismatched, %d non-$FF fill",
                        WANT, #img, npages, bad, fill))
    if first then print("BOOTTEST: first " .. first) end
    print((bad == 0 and fill == 0)
          and "BOOTTEST: PASS -- the booted image is byte-identical to the file, every page"
          or  "BOOTTEST: FAIL")
end

local function report()
    if #img > FIXED then return report_banked() end
    local bad, first = 0, nil
    for i = 1, #img do
        local got = sp:readv_u8(0x0800 + i - 1)
        if got ~= img:byte(i) then
            bad = bad + 1
            if not first then
                first = string.format("$%04X: served $%02X, file $%02X",
                                      0x0800 + i - 1, got, img:byte(i))
            end
        end
    end
    local fill = 0
    for a = 0x0800 + #img, 0x47FF do
        if sp:readv_u8(a) ~= 0xFF then fill = fill + 1 end
    end
    print(string.format("BOOTTEST: %s: %d bytes, %d mismatched, %d non-$FF fill",
                        WANT, #img, bad, fill))
    if first then print("BOOTTEST: first " .. first) end
    print((bad == 0 and fill == 0)
          and "BOOTTEST: PASS -- the booted image is byte-identical to the file"
          or  "BOOTTEST: FAIL")
end

_G._bt = emu.add_machine_frame_notifier(function()
    n = n + 1
    sp = sp or manager.machine.devices[":maincpu"].spaces["program"]
    if not img then
        local f = io.open(WANT, "rb")
        if not f then
            print("BOOTTEST: FAIL -- cannot open " .. WANT)
            manager.machine:exit()
            return
        end
        img = f:read("*a")
        f:close()
    end
    if n % 30 ~= 0 then return end
    if sp:readv_u8(0xFC06) == 0x80 then       -- FN_BOOT_FAILED
        print(string.format("BOOTTEST: REFUSED err=%d", sp:readv_u8(0xFC08)))
        manager.machine:exit()
        return
    end
    if looks_swapped() then
        report()
        manager.machine:exit()
    elseif n > DEADLINE then
        print("BOOTTEST: FAIL -- the image never appeared in the window")
        report()
        manager.machine:exit()
    end
end)
