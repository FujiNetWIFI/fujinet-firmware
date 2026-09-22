-- soak.lua: verify one DBC-streamed cart image, headless.
--
-- Ridden by tools/soak.sh with fujiboot (BOOT_PATH=/soak.bin) as the cart:
-- press '1' at t=3 to launch fujiboot, watch the boot state at 3C06H, and
-- when the swap happens (the 'F','N' magic pair vanishes -- every image in
-- the soak corpus is claim-less) read the ENTIRE served window 2000H-3FFFH
-- and byte-compare it against the expected mapping in $SOAK_EXPECT (the 8K
-- astromap output soak.sh precomputes: power-of-two images mirror, odd
-- sizes pad with FFH). All polled and dumped offsets are side-effect-free
-- once the mailbox is dead. Then press '1' again to launch the booted
-- image's menu entry and sample the Z80 PC for 300 frames -- how often it
-- lands in cart space is reported, but the window compare is the verdict.
--
-- Prints exactly one line:  soak: PASS pc=N/300 | soak: FAIL <why>
--
--   SOAK_EXPECT   path to the expected 8K window image (required)
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

local phase = "boot"        -- boot -> swapped -> sample
local down = false
local press_at = 3
local sample_left = 300
local pc_in_cart = 0
local finished = false

local function done(msg)
    print("soak: " .. msg)
    finished = true
    manager.machine:exit()
end

emu.register_frame(function()
    if finished then return end
    local t = manager.machine.time.seconds
    local port = manager.machine.ioport.ports[":KEYPAD3"]
    if port == nil then return end
    local mem = manager.machine.devices[":maincpu"].spaces["program"]

    if expect == nil or #expect ~= 8192 then
        return done("FAIL no expected window at $SOAK_EXPECT")
    end
    if t >= timeout then
        return done(string.format("FAIL timeout in %s (state=%02X err=%02X)",
                                  phase, mem:read_u8(0x3C06), mem:read_u8(0x3C08)))
    end

    if not down and press_at and t >= press_at then
        port:field(0x10):set_value(1)
        down = true
    elseif down and press_at and t >= press_at + 2 then
        port:field(0x10):clear_value()
        down = false
        press_at = nil
    end

    if phase == "boot" then
        -- The magic pair vanishing is the swap, for a claim-less image.
        -- Test it FIRST: once the swap lands, every mailbox offset reads
        -- game bytes, and interpreting those as a boot state is a lie.
        local swapped = mem:read_u8(0x3C09) ~= 0x46 or mem:read_u8(0x3C0A) ~= 0x4E
        if not swapped then
            local state = mem:read_u8(0x3C06)
            if state >= 0x80 then
                return done(string.format("FAIL boot err=%02X", mem:read_u8(0x3C08)))
            end
        end
        if swapped then
            for off = 0, 8191 do
                if mem:read_u8(0x2000 + off) ~= string.byte(expect, off + 1) then
                    return done(string.format(
                        "FAIL window mismatch at %04X: %02X != %02X",
                        0x2000 + off, mem:read_u8(0x2000 + off),
                        string.byte(expect, off + 1)))
                end
            end
            phase = "swapped"
            press_at = t + 2
        end
    elseif phase == "swapped" then
        if press_at == nil then phase = "sample" end
    else
        local pc = manager.machine.devices[":maincpu"].state["PC"].value
        if pc >= 0x2000 and pc < 0x4000 then
            pc_in_cart = pc_in_cart + 1
        end
        sample_left = sample_left - 1
        if sample_left == 0 then
            return done(string.format("PASS pc=%d/300", pc_in_cart))
        end
    end
end)
