-- soaktest.lua -- push one cartridge, boot it, and compare every byte of every
-- bank, selected through the scheme's own hotspots, against the file.
--
-- Env: BOOT_IMAGE (the file), SOAK_SCHEME (FLAT/F8/F6/F4/FA/E0/UA/FE/CV).

local WIN, BASE = 0x1000, 0x1000
local FN_CLAIM = 0x1F10

local want_file = os.getenv("BOOT_IMAGE")
local scheme = os.getenv("SOAK_SCHEME") or "FLAT"
local sp, done, waited = nil, false, 0

local function claimed()
    return sp:readv_u8(FN_CLAIM) == 0x46 and sp:readv_u8(FN_CLAIM + 1) == 0x55
end

-- Where a scheme's cartridge RAM is read: RAM, not file bytes, so not compared.
local function is_ram(a)
    if scheme == "FA" then return a >= 0x1100 and a <= 0x11FF end
    if scheme:sub(-2) == "SC" then return a >= 0x1080 and a <= 0x10FF end
    if scheme == "CV" then return a >= 0x1000 and a <= 0x13FF end
    return false
end

-- The scheme's lowest hotspot, and a function re-selecting the bank under test.
-- MAME's readv_u8 and read_u8 are plain read_byte()s with side effects, so a
-- sweep trips the hotspots it reads, and a read tap switches AFTER the read:
-- every address from the lowest hotspot up is re-selected before it is read.
local hotspot_lo, reselect

-- Compare the served window against `len` bytes of `img` starting at `off`,
-- repeated to fill the window if the slice is shorter (the 2K mirror).
local function compare(img, off, len, label, quiet)
    local bad, first = 0, nil
    for i = 0, WIN - 1 do
        if is_ram(BASE + i) then goto continue end
        local a = BASE + i
        -- The switch lands after the read, so this byte is still the right one.
        if hotspot_lo and a >= hotspot_lo then reselect() end
        local got = sp:readv_u8(a)
        local exp = img:byte(off + (i % len) + 1)
        if got ~= exp then
            bad = bad + 1
            if not first then
                first = string.format("$%04X: want $%02X got $%02X",
                                      BASE + i, exp, got)
            end
        end
        ::continue::
    end
    if bad > 0 then
        if not quiet then
            print(string.format("FAIL: %s %s: %d of %d bytes differ. First at %s",
                                scheme, label, bad, WIN, first))
        end
        return false
    end
    return true
end

_G._soak = emu.add_machine_frame_notifier(function()
    if done then return end
    sp = sp or manager.machine.devices[":maincpu"].spaces["program"]

    if claimed() then
        waited = waited + 1
        if waited > 1800 then
            print("FAIL: " .. scheme .. ": the swap never happened")
            done = true
            manager.machine:exit()
        end
        return
    end
    done = true

    local f = io.open(want_file, "rb")
    if not f then
        print("FAIL: cannot open " .. tostring(want_file))
        manager.machine:exit()
        return
    end
    local img = f:read("*a")
    f:close()

    local ok = true

    if scheme == "FLAT" or scheme == "CV" then
        -- 2K mirrors into the window. CV's RAM read window is skipped, leaving
        -- the ROM around it, including MAME's mirror at $1800-$1BFF.
        ok = compare(img, 0, math.min(#img, WIN), "flat")

    elseif scheme == "F8" or scheme == "F6" or scheme == "F4"
        or scheme == "FA" or scheme == "F8SC" or scheme == "F6SC"
        or scheme == "F4SC" then
        local lo, n
        if scheme:sub(1, 2) == "F8" then lo, n = 0x1FF8, 2
        elseif scheme:sub(1, 2) == "F6" then lo, n = 0x1FF6, 4
        elseif scheme:sub(1, 2) == "F4" then lo, n = 0x1FF4, 8
        else lo, n = 0x1FF8, 3 end                  -- FA
        hotspot_lo = lo
        for b = 0, n - 1 do
            reselect = function() sp:write_u8(lo + b, 0) end
            sp:write_u8(lo + b, 0)                  -- the hotspot, as a write
            if not compare(img, b * WIN, WIN, string.format("bank %d", b)) then
                ok = false
                break
            end
        end

    elseif scheme == "E0" then
        -- Three 1K slots; the top slice is hardwired to slice 7.
        for s = 0, 7 do
            local function sel()
                sp:write_u8(0x1FE0 + s, 0)          -- slot 0 <- slice s
                sp:write_u8(0x1FE8 + s, 0)          -- slot 1 <- slice s
                sp:write_u8(0x1FF0 + s, 0)          -- slot 2 <- slice s
            end
            sel()
            local bad = 0
            for i = 0, WIN - 1 do
                local a = BASE + i
                -- E0's hotspots sit in the fixed top slice: same re-select rule.
                if a >= 0x1FE0 then sel() end
                local slice = (i < 0xC00) and s or 7
                local exp = img:byte(slice * 0x400 + (i % 0x400) + 1)
                if sp:readv_u8(a) ~= exp then bad = bad + 1 end
            end
            if bad > 0 then
                print(string.format("FAIL: E0 slice %d: %d bytes differ", s, bad))
                ok = false
                break
            end
        end

    elseif scheme == "UA" then
        for b = 0, 1 do
            sp:write_u8(0x0200 + b * 0x40, 0)       -- the tap is below $1000
            if not compare(img, b * WIN, WIN, string.format("bank %d", b)) then
                ok = false
                break
            end
        end

    elseif scheme == "FE" then
        -- FE switches on the access after $01FE, which the debugger's own reads
        -- disturb; check only that the window is one whole bank. test_vcsmap.c
        -- covers the rule against MAME's handler.
        for _ = 1, 4 do
            sp:read_u8(0x01FE)
            sp:read_u8(0x1000)
            local b0 = compare(img, 0, WIN, "bank 0", true)
            local b1 = compare(img, WIN, WIN, "bank 1", true)
            if not (b0 or b1) then
                print("FAIL: FE: the window matches neither bank")
                ok = false
                break
            end
        end

    else
        print("FAIL: unknown scheme " .. scheme)
        ok = false
    end

    if ok then
        print(string.format("PASS: %s -- every served bank matches %s",
                            scheme, want_file:match("[^/]+$")))
    end
    manager.machine:exit()
end)
