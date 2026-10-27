-- soak.lua: verify one streamed cart image, run by tools/soak.sh with fujiboot as the cart.
-- When the 'F','N' magic at 2C09/2C0A vanishes (soak images are claim-less), compare the
-- served blocks to $SOAK_EXPECT, then report how often the PC is in cart space over 300 frames.
--
-- Check the magic first: after the swap the mailbox offsets read game bytes. Lua reads fire
-- the device's read handlers, so pre-swap poll only the $2C00 status page, never $2D00-$2FFF.
--
--   soak: PASS pc=N/300 | soak: FAIL <why>
--
--   SOAK_EXPECT   path to the expected 8K image (required)
--   SOAK_TIMEOUT  emulated-seconds budget (default 60)

local expect_path = os.getenv("SOAK_EXPECT")
local timeout = tonumber(os.getenv("SOAK_TIMEOUT") or "60")

local expect
do
    local f = expect_path and io.open(expect_path, "rb")
    if f then
        expect = f:read("*a")
        f:close()
    end
end

local phase = "boot"        -- boot -> sample
local sample_left = 300
local pc_in_cart = 0
local finished = false

local function done(msg)
    print("soak: " .. msg)
    finished = true
    manager.machine:exit()
end

-- image 1000-1FFF is served at 2000-2FFF.
local function served(off)
    if off < 0x1000 then return off end
    return off + 0x1000
end

-- Keep the handle in a global or it is collected and the callback stops.
_G.SOAK_SUB = emu.add_machine_frame_notifier(function()
    if finished then return end
    local t = manager.machine.time.seconds
    local mem = manager.machine.devices[":maincpu"].spaces["program"]

    if expect == nil or #expect ~= 8192 then
        return done("FAIL no expected image at $SOAK_EXPECT")
    end
    if t >= timeout then
        return done(string.format("FAIL timeout in %s (state=%02X err=%02X)",
                                  phase, mem:read_u8(0x2C06), mem:read_u8(0x2C08)))
    end

    if phase == "boot" then
        local swapped = mem:read_u8(0x2C09) ~= 0x46 or mem:read_u8(0x2C0A) ~= 0x4E
        if not swapped then
            local state = mem:read_u8(0x2C06)
            if state >= 0x80 then
                return done(string.format("FAIL boot err=%02X", mem:read_u8(0x2C08)))
            end
            return
        end
        for off = 0, 8191 do
            local got = mem:read_u8(served(off))
            if got ~= string.byte(expect, off + 1) then
                return done(string.format(
                    "FAIL window mismatch at img %04X: %02X != %02X",
                    off, got, string.byte(expect, off + 1)))
            end
        end
        phase = "sample"
    else
        local pc = manager.machine.devices[":maincpu"].state["PC"].value
        if pc < 0x1000 or (pc >= 0x2000 and pc < 0x3000) then
            pc_in_cart = pc_in_cart + 1
        end
        sample_left = sample_left - 1
        if sample_left == 0 then
            return done(string.format("PASS pc=%d/300", pc_in_cart))
        end
    end
end)
