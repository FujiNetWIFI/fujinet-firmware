-- dirtest.lua -- M3: browse to a NAMED file and boot it.
--
-- Reads the rendered text planes back into characters, finds the row whose
-- name matches BOOT_NAME, walks the cursor there with the joystick, presses
-- fire, and requires the served window to be byte-identical to that file.
--
-- Navigating by NAME rather than by a hardcoded row count is the point. A test
-- that pressed down three times would pass with the list drawn in the wrong
-- order, truncated, off by a row, or not drawn at all -- every failure mode
-- the browser actually has.
--
-- Every wait is on the program's own rendered state, never on a frame number,
-- so a slow socket round trip cannot turn into a flake.

package.path = (os.getenv("A2600_EMU") or ".") .. "/?.lua;" .. package.path
local font = require("vcsfont")

local T_BASE, T_PLANE_LEN, T_CELL_H, T_COLS = 0x1800, 0x80, 6, 12
local FN_CLAIM = 0x1F10
local ROW0, NROWS = 3, 14

local want_name = (os.getenv("BOOT_NAME") or "VCSGAME.BIN"):upper()
local want_file = os.getenv("BOOT_IMAGE")

local sp, phase, waited, target, presses = nil, "list", 0, nil, 0

-- One text row, as the sequence of glyph keys actually on screen.
local function read_keys(row)
    local out = {}
    for col = 0, T_COLS - 1 do
        local plane = col // 2
        local left = (col % 2) == 0
        local key = 0
        for line = 0, 4 do                     -- 5 ink rows; the 6th is leading
            local b = sp:readv_u8(T_BASE + plane * T_PLANE_LEN
                                         + row * T_CELL_H + line)
            local ink = left and ((b >> 5) & 7) or ((b >> 1) & 7)
            key = (key << 3) | ink
        end
        out[col] = key
    end
    return out
end

-- The same row as text, for error messages only. Lossy on purpose.
local function read_row(row)
    local keys, s = read_keys(row), ""
    for col = 0, T_COLS - 1 do
        s = s .. (font.glyph[keys[col]] or "?")
    end
    return s
end

-- What `name` would look like on screen, as glyph keys.
local function name_keys(name)
    local out = {}
    for i = 1, #name do
        out[i - 1] = font.key[name:sub(i, i)] or font.key["?"]
    end
    return out
end

local function claimed()
    return sp:readv_u8(FN_CLAIM) == 0x46 and sp:readv_u8(FN_CLAIM + 1) == 0x55
end

local function press(field, on)
    local p = manager.machine.ioport.ports[":joyport1:joy:JOY"]
    p.fields[field]:set_value(on and 1 or 0)
end

_G._dirtest = emu.add_machine_frame_notifier(function()
    if phase == "done" then return end
    sp = sp or manager.machine.devices[":maincpu"].spaces["program"]
    waited = waited + 1
    if waited > 3600 then
        print("FAIL: timed out in phase " .. phase)
        manager.machine:exit()
        return
    end

    if phase == "list" then
        -- Wait for a listing to appear, then find the wanted name in it.
        local keys, here = {}, nil
        local cur = font.key[">"]
        for r = 0, NROWS - 1 do
            keys[r] = read_keys(ROW0 + r)
            if keys[r][0] == cur then here = r end
        end
        if not here then return end            -- not drawn yet

        -- Compare RENDERED FORMS, not decoded text. At 3x5 'S' and '5' are the
        -- same glyph and so are 'O' and '0', so decoding is lossy and a
        -- decoded-text comparison would fail on any name containing them --
        -- which is most of them.
        local wk = name_keys(want_name)
        local n = math.min(#want_name, T_COLS - 1)
        for r = 0, NROWS - 1 do
            local ok = true
            for i = 0, n - 1 do
                if keys[r][i + 1] ~= wk[i] then ok = false; break end
            end
            -- and the name must END there, not merely start with it
            if ok and n < T_COLS - 1 and keys[r][n + 1] ~= font.key[" "] then
                ok = false
            end
            if ok then target = r end
        end
        if not target then
            -- The dump used a `rows` table that was never built, so this path
            -- threw inside the notifier instead of reporting -- and a throw in
            -- a frame notifier is swallowed, so it simply fired again next
            -- frame, forever. Read the rows here, and latch so the exit that
            -- follows is not raced by another frame.
            print("FAIL: " .. want_name .. " is not in the listing:")
            for r = 0, NROWS - 1 do
                local line = ""
                for col = 0, T_COLS - 1 do
                    line = line .. (font.glyph[keys[r][col]] or "?")
                end
                if line:gsub("%s", "") ~= "" then
                    print(string.format("  row %2d %q", r, line))
                end
            end
            phase = "done"
            manager.machine:exit()
            return
        end
        print(string.format("listing drawn; %q is at row %d, cursor at %d",
                            want_name, target, here))
        if here == target then
            phase = "fire"
        else
            presses = target - here
            phase = "walk"
        end
        waited = 0
        return
    end

    if phase == "walk" then
        -- One step per two frames: the client edge-detects, so the stick has
        -- to be released between steps.
        if presses <= 0 then phase = "fire"; waited = 0; return end
        if waited % 4 == 1 then
            press("P1 Down", true)
        elseif waited % 4 == 3 then
            press("P1 Down", false)
            presses = presses - 1
        end
        return
    end

    if phase == "fire" then
        local here
        for r = 0, NROWS - 1 do
            if read_keys(ROW0 + r)[0] == font.key[">"] then here = r end
        end
        if here ~= target then
            print(string.format("FAIL: cursor landed on row %s, wanted %d",
                                tostring(here), target))
            manager.machine:exit()
            return
        end
        print("cursor on target; pressing fire")
        press("P1 Button 1", true)
        phase = "fired"
        waited = 0
        return
    end

    if phase == "fired" then
        if waited == 3 then press("P1 Button 1", false) end
        if claimed() then return end           -- still the browser
        phase = "check"
        waited = 0
        return
    end

    if phase == "check" then
        print("swap detected: the claim is gone")
        local f = io.open(want_file, "rb")
        if not f then
            print("FAIL: cannot open " .. tostring(want_file))
            manager.machine:exit()
            return
        end
        local img = f:read("*a")
        f:close()

        local bad, first = 0, nil
        for i = 0, 0xFFF do
            local page = (0x1000 + i) & 0xFF00
            if page ~= 0x1D00 and page ~= 0x1E00 then
                local got, exp = sp:readv_u8(0x1000 + i), img:byte(i + 1)
                if got ~= exp then
                    bad = bad + 1
                    if not first then
                        first = string.format("$%04X: want $%02X got $%02X",
                                              0x1000 + i, exp, got)
                    end
                end
            end
        end
        if bad > 0 then
            print(string.format("FAIL: %d served bytes differ. First at %s",
                                bad, first))
        else
            print(string.format("PASS: browsed to %q by name, booted it, "
                                .. "and all 4096 served bytes match", want_name))
        end
        manager.machine:exit()
    end
end)
