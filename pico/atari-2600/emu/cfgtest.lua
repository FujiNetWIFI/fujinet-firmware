-- cfgtest.lua -- M7: drive CONFIG all the way from power-on to a booted game.
--
-- Host screen -> mount host 0 -> browse the root -> DESCEND into a subfolder
-- -> boot a file inside it -> the served window is byte-identical to that
-- file. Every step is found by reading the rendered text planes back, so the
-- test navigates by NAME and never by a hardcoded row count: a listing drawn
-- in the wrong order, truncated, off by a row, or not drawn at all all fail.
--
-- The descent is the part worth having. It is the only thing that exercises
-- the cartridge-held working directory end to end -- FN_HOT_PATH_CH building
-- it, FN_PATH_TX emitting it for OPEN_DIRECTORY, FN_PATH_TXRAW plus the
-- published length emitting it again for SET_DEVICE_FULLPATH -- and the
-- console has 128 bytes of RAM, so if any of that were wrong there is nowhere
-- the path could have come from instead.
--
-- Env: BOOT_IMAGE (the file on disk), CFG_DIR (the folder), CFG_NAME (the
-- file), and it drives whatever host slot 0 is.

package.path = (os.getenv("A2600_EMU") or ".") .. "/?.lua;" .. package.path
local font = require("vcsfont")

local T_BASE, T_PLANE_LEN, T_CELL_H, T_COLS = 0x1800, 0x80, 6, 12
local FN_CLAIM = 0x1F10
local ROW0, NROWS = 2, 14

local want_dir  = (os.getenv("CFG_DIR") or "VCS/"):upper()
local want_name = (os.getenv("CFG_NAME") or "DEEP.BIN"):upper()
local want_file = os.getenv("BOOT_IMAGE")

local sp, phase, waited, target, presses = nil, "hosts", 0, nil, 0

local function read_keys(row)
    local out = {}
    for col = 0, T_COLS - 1 do
        local plane, left, key = col // 2, (col % 2) == 0, 0
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

local function read_row(row)
    local keys, s = read_keys(row), ""
    for col = 0, T_COLS - 1 do
        s = s .. (font.glyph[keys[col]] or "?")
    end
    return s
end

-- What `name` would look like on screen, as glyph keys. Comparing RENDERED
-- FORMS rather than decoded text matters: at 3x5 'S' and '5' are the same
-- glyph and so are 'O' and '0', so decoding is lossy and a decoded comparison
-- would fail on most real filenames.
local function name_keys(name)
    local out = {}
    for i = 1, #name do
        out[i - 1] = font.key[name:sub(i, i)] or font.key["?"]
    end
    return out
end

-- Find the list row whose name (column 1 onward, past the cursor gutter)
-- is `name`, and the row the cursor is on. Returns nil if nothing is drawn.
local function find(name)
    local cur, here, hit = font.key[">"], nil, nil
    local wk = name_keys(name)
    local n = math.min(#name, T_COLS - 1)
    for r = 0, NROWS - 1 do
        local keys = read_keys(ROW0 + r)
        if keys[0] == cur then here = r end
        local ok = true
        for i = 0, n - 1 do
            if keys[i + 1] ~= wk[i] then ok = false; break end
        end
        -- and the name must END there, not merely start with it
        if ok and n < T_COLS - 1 and keys[n + 1] ~= font.key[" "] then
            ok = false
        end
        if ok then hit = r end
    end
    return here, hit
end

local function dump(what)
    print("FAIL: " .. what .. "; the screen reads:")
    for r = 0, 20 do
        local s = read_row(r)
        if s:gsub("%s", "") ~= "" then
            print(string.format("  row %2d %q", r, s))
        end
    end
end

local function claimed()
    return sp:readv_u8(FN_CLAIM) == 0x46 and sp:readv_u8(FN_CLAIM + 1) == 0x55
end

local function press(field, on)
    local p = manager.machine.ioport.ports[":joyport1:joy:JOY"]
    p.fields[field]:set_value(on and 1 or 0)
end

-- Walk the cursor from `here` to `target` and fire. Returns the next phase.
local function step_walk(nextphase)
    if presses <= 0 then return nextphase end
    if waited % 4 == 1 then
        press("P1 Down", true)                 -- the client edge-detects, so
    elseif waited % 4 == 3 then                --   the stick has to be
        press("P1 Down", false)                --   released between steps
        presses = presses - 1
    end
    return nil
end

_G._cfgtest = emu.add_machine_frame_notifier(function()
    sp = sp or manager.machine.devices[":maincpu"].spaces["program"]
    waited = waited + 1
    if waited > 2400 then
        dump("timed out in phase " .. phase)
        manager.machine:exit()
        return
    end

    -- ---- the host screen -------------------------------------------------
    if phase == "hosts" then
        -- Wait for a cursor to appear on the list, whatever slot 0 is called.
        local here = select(1, find("\1"))     -- a key that matches nothing
        if not here then return end
        if here ~= 0 then
            dump("the host cursor started on row " .. here .. ", not 0")
            manager.machine:exit()
            return
        end
        print("host screen up, cursor on slot 0: " .. read_row(ROW0))
        press("P1 Button 1", true)
        phase, waited = "hostfired", 0
        return
    end

    if phase == "hostfired" then
        if waited == 3 then press("P1 Button 1", false) end
        if waited < 6 then return end
        phase, waited = "root", 0
        return
    end

    -- ---- the root listing, looking for the folder ------------------------
    if phase == "root" then
        local here, hit = find(want_dir)
        if not here then return end            -- not drawn yet
        if not hit then
            -- The listing is up but the folder is not on this page. That is a
            -- real failure, not a wait: nothing is going to add it.
            if waited > 240 then
                dump(want_dir .. " is not in the root listing")
                manager.machine:exit()
            end
            return
        end
        print(string.format("root listed; %q is at row %d, cursor at %d",
                            want_dir, hit, here))
        target, presses = hit, hit - here
        phase, waited = "walkdir", 0
        return
    end

    if phase == "walkdir" then
        local nxt = step_walk("firedir")
        if nxt then phase, waited = nxt, 0 end
        return
    end

    if phase == "firedir" then
        local here = select(1, find("\1"))
        if here ~= target then
            dump(string.format("cursor landed on row %s, wanted %d",
                               tostring(here), target))
            manager.machine:exit()
            return
        end
        print("cursor on the folder; pressing fire to descend")
        press("P1 Button 1", true)
        phase, waited = "descending", 0
        return
    end

    if phase == "descending" then
        if waited == 3 then press("P1 Button 1", false) end
        if waited < 6 then return end
        phase, waited = "sub", 0
        return
    end

    -- ---- inside the folder -----------------------------------------------
    if phase == "sub" then
        local here, hit = find(want_name)
        if not here then return end
        if not hit then
            if waited > 240 then
                dump(want_name .. " is not in " .. want_dir
                     .. " -- the descent did not happen, or went somewhere else")
                manager.machine:exit()
            end
            return
        end
        print(string.format("descended into %q; %q is at row %d, cursor at %d",
                            want_dir, want_name, hit, here))
        target, presses = hit, hit - here
        phase, waited = "walkfile", 0
        return
    end

    if phase == "walkfile" then
        local nxt = step_walk("firefile")
        if nxt then phase, waited = nxt, 0 end
        return
    end

    if phase == "firefile" then
        local here = select(1, find("\1"))
        if here ~= target then
            dump(string.format("cursor landed on row %s, wanted %d",
                               tostring(here), target))
            manager.machine:exit()
            return
        end
        print("cursor on the file; pressing fire to boot")
        press("P1 Button 1", true)
        phase, waited = "booting", 0
        return
    end

    if phase == "booting" then
        if waited == 3 then press("P1 Button 1", false) end
        if claimed() then return end           -- still CONFIG
        phase, waited = "check", 0
        return
    end

    -- ---- the served window -----------------------------------------------
    if phase == "check" then
        print("swap detected: the claim is gone, the image is a game")
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
            local got, exp = sp:readv_u8(0x1000 + i), img:byte(i + 1)
            if got ~= exp then
                bad = bad + 1
                if not first then
                    first = string.format("$%04X: want $%02X got $%02X",
                                          0x1000 + i, exp, got)
                end
            end
        end
        if bad > 0 then
            print(string.format("FAIL: %d of 4096 served bytes differ. "
                                .. "First at %s", bad, first))
        else
            print(string.format("PASS: CONFIG mounted host 0, descended into "
                                .. "%q, booted %q, and all 4096 served bytes "
                                .. "match", want_dir, want_name))
        end
        manager.machine:exit()
    end
end)
