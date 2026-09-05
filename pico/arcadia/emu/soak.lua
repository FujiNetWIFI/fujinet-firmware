-- soak.lua: verify one DBC-streamed cart image, headless.
--
-- Ridden by tools/soak.sh with fujiboot (BOOT_PATH=/soak.bin) as the cart.
-- fujiboot auto-runs at reset (no console menu to press through), so this
-- just watches the boot state at 2C06H, and when the swap happens -- the
-- 'F','N' magic pair at 2C09/2C0A vanishes (every soak image is claim-less)
-- -- reads the served blocks (0000-0FFF and 2000-2FFF) and byte-compares
-- them against the expected mapping in $SOAK_EXPECT (the 8K arcmap output
-- soak.sh precomputes: image bytes then 0xFF fill, == MAME's stock STD
-- mapper). All polled and dumped offsets are side-effect-free once the
-- mailbox is dead. Then sample the 2650 PC for 300 frames -- how often it
-- lands in cart space is reported, but the window compare is the verdict.
--
-- The magic-vanished test comes FIRST: after the swap every mailbox offset
-- reads game bytes, so interpreting them as a boot state would be a lie.
-- Pre-swap it polls ONLY the status page ($2C00) -- lua reads DO fire the
-- device's read handlers, so it must never touch the $2D00-$2FFF hotspots.
--
-- Prints exactly one line:  soak: PASS pc=N/300 | soak: FAIL <why>
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

-- served console address for image offset 0..8191: 0000-0FFF is block 1,
-- 1000-1FFF maps to 2000-2FFF (block 2).
local function served(off)
    if off < 0x1000 then return off end
    return off + 0x1000
end

emu.register_frame(function()
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
        -- cart code executes in block 1 ($0000-$0FFF) or block 2 ($2000-$2FFF)
        if pc < 0x1000 or (pc >= 0x2000 and pc < 0x3000) then
            pc_in_cart = pc_in_cart + 1
        end
        sample_left = sample_left - 1
        if sample_left == 0 then
            return done(string.format("PASS pc=%d/300", pc_in_cart))
        end
    end
end)
