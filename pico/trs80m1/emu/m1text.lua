-- m1text.lua -- drives a MAME Model I from a scenario's step lines and prints a verdict.
--
-- The steps file is $M1_STEPS: lines starting with ';' are run.sh's, the rest are
--   config PORT FIELD VALUE   set a configuration switch before the first frame
--   wait TEXT [SECS]          until the screen shows TEXT (default 30 emulated seconds)
--   type TEXT                 post keys; \r is ENTER. A capital is the unshifted key,
--                             as on the Model I, and a small letter the shifted one
--   key NAME [SECS]           hold Break, Clear or Enter for SECS (default 0.2)
--   sleep SECS
--   must TEXT                 the screen shows TEXT now
--   dump                      print the screen
--   pass                      print the verdict and exit ($M1_KEEP=1: and keep running)
-- TEXT is matched exactly; quotes around it are dropped, and \" inside is a quote.

local steps = {}
local path = os.getenv("M1_STEPS")
if path then
    for line in io.lines(path) do
        if not line:match("^%s*;") and line:match("%S") then
            table.insert(steps, line)
        end
    end
end

local machine = manager.machine
local program = machine.devices[":maincpu"].spaces["program"]
local step_idx = 1
local step_start = nil
local held_field = nil
local held_until = 0
local verdict_given = false

local function now_secs()
    return machine.time:as_double()
end

-- The Model I has no lowercase without the mod: video RAM bit 6 is missing, so
-- 40h-5Fh read back as 00h-1Fh.
local function screen_lines()
    local lines = {}
    for row = 0, 15 do
        local chars = {}
        for col = 0, 63 do
            local byte = program:read_u8(0x3C00 + row * 64 + col)
            if byte >= 0x80 then
                byte = 0x23
            elseif byte < 0x20 then
                byte = byte + 0x40
            end
            chars[#chars + 1] = string.char(byte)
        end
        lines[#lines + 1] = table.concat(chars)
    end
    return lines
end

local function screen_text()
    return table.concat(screen_lines(), "\n")
end

local function dump()
    print("---- screen at " .. string.format("%.2f", now_secs()) .. " s ----")
    for _, line in ipairs(screen_lines()) do
        print("|" .. line .. "|")
    end
    io.stdout:flush()
end

local function shows(text)
    return screen_text():find(text, 1, true) ~= nil
end

local function finish(passed, why)
    if verdict_given then
        return
    end
    verdict_given = true
    if not passed then
        dump()
    end
    print("VERDICT " .. (passed and "PASS" or "FAIL") .. (why and (" " .. why) or ""))
    io.stdout:flush()
    if os.getenv("M1_KEEP") ~= "1" then
        machine:exit()
    end
end

local function unquote(text)
    return (text:gsub('^"(.*)"$', "%1"):gsub("\\r", "\r"):gsub('\\"', '"'))
end

-- Splits `verb rest`, and a trailing number off rest when `numbered`.
local function parse(line, numbered)
    local verb, rest = line:match("^%s*(%S+)%s*(.-)%s*$")
    local secs = nil
    if numbered then
        local body, num = rest:match('^(.-)%s+([%d%.]+)$')
        if body and (body:match('"$') or not rest:match('^"')) then
            rest, secs = body, tonumber(num)
        end
    end
    return verb, unquote(rest), secs
end

local function find_field(name)
    for _, port in pairs(machine.ioport.ports) do
        local field = port.fields[name]
        if field then
            return field
        end
    end
    return nil
end

-- config lines apply before the machine runs.
local remaining = {}
for _, line in ipairs(steps) do
    local verb, rest = line:match("^%s*(%S+)%s*(.-)%s*$")
    if verb == "config" then
        local port_tag, field_name, value = rest:match('^(%S+)%s+"(.-)"%s+(%S+)$')
        local port = machine.ioport.ports[port_tag]
        local field = port and port.fields[field_name]
        if field then
            field.user_value = tonumber(value)
        else
            print("m1text: no field " .. tostring(port_tag) .. " " .. tostring(field_name))
        end
    else
        remaining[#remaining + 1] = line
    end
end
steps = remaining

local function run_step()
    local line = steps[step_idx]
    if not line then
        finish(true, "steps done")
        return
    end
    local verb, text, secs = parse(line, true)
    local tnow = now_secs()
    if not step_start then
        step_start = tnow
    end
    local advance = false
    if verb == "wait" then
        if shows(text) then
            advance = true
        elseif tnow - step_start > (secs or 30) then
            finish(false, "timeout waiting for " .. text)
            return
        end
    elseif verb == "type" then
        machine.natkeyboard:post(text)
        advance = true
    elseif verb == "key" then
        if not held_field then
            held_field = find_field(text)
            if not held_field then
                finish(false, "no key " .. text)
                return
            end
            held_field:set_value(1)
            held_until = tnow + (secs or 0.2)
        elseif tnow >= held_until then
            held_field:clear_value()
            held_field = nil
            advance = true
        end
    elseif verb == "sleep" then
        advance = tnow - step_start >= (tonumber(text) or 0)
    elseif verb == "must" then
        if not shows(text) then
            finish(false, "screen lacks " .. text)
            return
        end
        advance = true
    elseif verb == "dump" then
        dump()
        advance = true
    elseif verb == "pass" then
        finish(true)
        return
    else
        finish(false, "bad step " .. line)
        return
    end
    if advance then
        step_idx = step_idx + 1
        step_start = nil
    end
end

-- $M1_TRACE names a file that gets every CPU access to 37E0h-37EFh, with runs of
-- the same access collapsed into one line and a count, so polling loops stay short.
local trace_path = os.getenv("M1_TRACE")
if trace_path == "" then
    trace_path = nil
end
if trace_path then
    local trace_file = io.open(trace_path, "w")
    local pc_state = machine.devices[":maincpu"].state["PC"]
    local last_key = nil
    local repeats = 0
    local function note(kind, offset, data)
        local key = string.format("%s %04X %02X", kind, offset, data)
        if key == last_key then
            repeats = repeats + 1
            return
        end
        if last_key then
            trace_file:write(string.format("%s x%d\n", last_key, repeats))
        end
        last_key = key
        repeats = 1
        trace_file:write(string.format("@%.6f pc=%04X\n", now_secs(), pc_state.value))
    end
    M1TRACE_READ = program:install_read_tap(0x37E0, 0x37EF, "m1trace_read",
        function(offset, data, mask)
            note("R", offset, data)
        end)
    M1TRACE_WRITE = program:install_write_tap(0x37E0, 0x37EF, "m1trace_write",
        function(offset, data, mask)
            note("W", offset, data)
        end)
    M1TRACE_STOP = emu.add_machine_stop_notifier(function()
        if last_key then
            trace_file:write(string.format("%s x%d\n", last_key, repeats))
        end
        trace_file:close()
    end)
end

M1TEXT_FRAME = emu.add_machine_frame_notifier(run_step)
