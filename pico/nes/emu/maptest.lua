-- maptest.lua -- the soak's bank check: boot a synthetic image through the
-- loader, then drive its mapper registers and verify what every PRG and CHR
-- slot shows against an expectation written in Lua from the nesdev register
-- descriptions -- deliberately NOT nesmap.c, which is what is under test.
--
-- The corpus images (tools/mkcorpus.py) stamp every 1K bank: 'P'/'C', bank
-- low, bank high, ~bank low. A slot's stamp is read with readv_u8, which has
-- no side effects; register writes use write_u8, which do.
--
--   MAPTEST_MAPPER   the image's mapper number (required)
--   MAPTEST_PRG16    PRG size in 16K units;  MAPTEST_CHR8  CHR-ROM size in 8K units (0 = RAM)
--   MAPTEST_TIMEOUT  seconds to wait for the load (default 60)
local T = dofile(os.getenv("NES_EMU_DIR") .. "/nestext.lua")
local mapper = tonumber(os.getenv("MAPTEST_MAPPER"))
local prg16 = tonumber(os.getenv("MAPTEST_PRG16") or "2")
local chr8 = tonumber(os.getenv("MAPTEST_CHR8") or "0")
local timeout = tonumber(os.getenv("MAPTEST_TIMEOUT") or "60")
local cpu = T.cpu_space()
local ppu = T.ppu_space()
local nprg8 = prg16 * 2            -- 8K PRG banks
local nchr1 = chr8 * 8             -- 1K CHR-ROM banks
local phase = 0
local fails = 0
local checks = 0

local function stamp_at(space, addr, kind)
  local k = space:readv_u8(addr)
  local lo = space:readv_u8(addr + 1)
  local hi = space:readv_u8(addr + 2)
  local nlo = space:readv_u8(addr + 3)
  if k ~= string.byte(kind) or nlo ~= (255 - lo) then return nil end
  return lo + hi * 256
end

-- PRG slot s (8K at $8000 + s*8K) must show 8K bank `bank`: check both 1K halves' stamps
local function expect_prg(s, bank, what)
  for k = 0, 7 do
    local got = stamp_at(cpu, 0x8000 + s * 0x2000 + k * 0x400, "P")
    checks = checks + 1
    if got ~= bank * 8 + k then
      fails = fails + 1
      print(string.format("FAIL %s: PRG slot %d 1K %d shows %s, expected %d", what, s, k, tostring(got), bank * 8 + k))
      return
    end
  end
end

local function expect_chr(s, bank1k, what)
  if nchr1 == 0 then return end        -- CHR-RAM: no stamps to read
  local got = stamp_at(ppu, s * 0x400, "C")
  checks = checks + 1
  if got ~= bank1k then
    fails = fails + 1
    print(string.format("FAIL %s: CHR slot %d shows %s, expected %d", what, s, tostring(got), bank1k))
  end
end

local function w(addr, v) cpu:write_u8(addr, v) end

-- MMC1: five serial writes, LSB first. A bit-7 write resets the shift
-- register AND forces control to mode 3 ($0C), on silicon and in MAME, so it
-- is done once at the start and never between registers.
local function mmc1_reg(addr, v)
  for i = 0, 4 do
    w(addr, (v >> i) & 1)
  end
end

local tests = {}

tests[0] = function()
  if prg16 == 1 then
    expect_prg(0, 0, "NROM-128 $8000"); expect_prg(1, 1, "NROM-128 $A000")
    expect_prg(2, 0, "NROM-128 $C000 mirror"); expect_prg(3, 1, "NROM-128 $E000 mirror")
  else
    for s = 0, 3 do expect_prg(s, s, "NROM-256") end
  end
  for s = 0, 7 do expect_chr(s, s, "NROM CHR") end
end

tests[2] = function()
  for b = 0, prg16 - 1 do
    w(0x8000, b)
    expect_prg(0, b * 2, "UxROM bank " .. b); expect_prg(1, b * 2 + 1, "UxROM bank " .. b)
    expect_prg(2, nprg8 - 2, "UxROM fixed"); expect_prg(3, nprg8 - 1, "UxROM fixed")
  end
  w(0x8000, prg16 + 1)              -- past the end wraps
  expect_prg(0, 2, "UxROM wrap")
end

tests[3] = function()
  for b = 0, chr8 - 1 do
    w(0x8000, b)
    for s = 0, 7 do expect_chr(s, b * 8 + s, "CNROM bank " .. b) end
  end
  for s = 0, 3 do expect_prg(s, s, "CNROM PRG") end
end

tests[7] = function()
  for b = 0, prg16 / 2 - 1 do
    w(0x8000, b)
    for s = 0, 3 do expect_prg(s, b * 4 + s, "AxROM bank " .. b) end
  end
end

tests[11] = function()
  for b = 0, prg16 / 2 - 1 do
    for c = 0, chr8 - 1 do
      w(0x8000, (c << 4) | b)
      for s = 0, 3 do expect_prg(s, b * 4 + s, "ColorDreams prg " .. b) end
      for s = 0, 7 do expect_chr(s, c * 8 + s, "ColorDreams chr " .. c) end
    end
  end
end

tests[66] = function()
  for b = 0, prg16 / 2 - 1 do
    for c = 0, chr8 - 1 do
      w(0x8000, (b << 4) | c)
      for s = 0, 3 do expect_prg(s, b * 4 + s, "GxROM prg " .. b) end
      for s = 0, 7 do expect_chr(s, c * 8 + s, "GxROM chr " .. c) end
    end
  end
end

tests[30] = function()
  for b = 0, prg16 - 1, 3 do
    w(0x8000, b)
    expect_prg(0, b * 2, "UNROM512 bank " .. b); expect_prg(1, b * 2 + 1, "UNROM512 bank " .. b)
    expect_prg(2, nprg8 - 2, "UNROM512 fixed"); expect_prg(3, nprg8 - 1, "UNROM512 fixed")
  end
end

tests[71] = function()
  for b = 0, prg16 - 1 do
    w(0xC000, b)
    expect_prg(0, b * 2, "Camerica bank " .. b); expect_prg(1, b * 2 + 1, "Camerica bank " .. b)
    expect_prg(3, nprg8 - 1, "Camerica fixed")
  end
end

tests[34] = function()
  if chr8 == 0 then
    for b = 0, prg16 / 2 - 1 do
      w(0x8000, b)
      for s = 0, 3 do expect_prg(s, b * 4 + s, "BNROM bank " .. b) end
    end
  else
    for b = 0, 1 do
      w(0x7FFD, b)
      for s = 0, 3 do expect_prg(s, b * 4 + s, "NINA-001 prg " .. b) end
    end
    for c = 0, chr8 * 2 - 1 do
      w(0x7FFE, c); w(0x7FFF, (c + 1) % (chr8 * 2))
      for s = 0, 3 do expect_chr(s, c * 4 + s, "NINA-001 chr0 " .. c) end
      for s = 0, 3 do expect_chr(4 + s, ((c + 1) % (chr8 * 2)) * 4 + s, "NINA-001 chr1 " .. c) end
    end
  end
end

tests[1] = function()
  w(0x8000, 0x80)                     -- reset: shift register clear, control |= $0C
  -- control = $0C (16K at $8000 switchable, last fixed), 8K CHR
  mmc1_reg(0x8000, 0x0C)
  for b = 0, prg16 - 1, 3 do
    mmc1_reg(0xE000, b)
    local hi = 0
    if prg16 == 32 then hi = 0 end
    expect_prg(0, (b % 16) * 2 + hi, "MMC1 mode3 bank " .. b); expect_prg(1, (b % 16) * 2 + 1 + hi, "MMC1 mode3 bank " .. b)
    expect_prg(2, math.min(nprg8, 32) - 2, "MMC1 mode3 fixed"); expect_prg(3, math.min(nprg8, 32) - 1, "MMC1 mode3 fixed")
  end
  -- mode 2: first fixed, $C000 switchable
  mmc1_reg(0x8000, 0x08)
  mmc1_reg(0xE000, 5 % prg16)
  expect_prg(0, 0, "MMC1 mode2 fixed"); expect_prg(2, (5 % prg16) * 2, "MMC1 mode2 bank 5")
  -- 32K mode
  mmc1_reg(0x8000, 0x00)
  mmc1_reg(0xE000, 2)
  for s = 0, 3 do expect_prg(s, (2 & 0x0E) * 2 + s, "MMC1 32K bank 2") end
  -- CHR: 4K mode
  if chr8 > 0 then
    mmc1_reg(0x8000, 0x1C)
    mmc1_reg(0xA000, 3 % (chr8 * 2)); mmc1_reg(0xC000, 1 % (chr8 * 2))
    for s = 0, 3 do expect_chr(s, (3 % (chr8 * 2)) * 4 + s, "MMC1 chr0") end
    for s = 0, 3 do expect_chr(4 + s, (1 % (chr8 * 2)) * 4 + s, "MMC1 chr1") end
    mmc1_reg(0x8000, 0x0C)
    mmc1_reg(0xA000, 2)                 -- 8K mode: bit 0 ignored, bank 1 (8K) = 1K banks 8..15
    for s = 0, 7 do expect_chr(s, 8 + s, "MMC1 chr 8K") end
  end
end

local function mmc3_set(reg, v) w(0x8000, reg); w(0x8001, v) end

tests[4] = function()
  local last = nprg8 - 1
  mmc3_set(6, 3); mmc3_set(7, 5)
  expect_prg(0, 3, "MMC3 R6"); expect_prg(1, 5, "MMC3 R7")
  expect_prg(2, last - 1, "MMC3 fixed -2"); expect_prg(3, last, "MMC3 fixed -1")
  w(0x8000, 0x40 | 6); w(0x8001, 9)    -- PRG mode 1: R6 at $C000
  expect_prg(0, last - 1, "MMC3 mode1 $8000"); expect_prg(2, 9, "MMC3 mode1 R6")
  if nchr1 > 0 then
    w(0x8000, 0)
    mmc3_set(0, 4); mmc3_set(1, 6); mmc3_set(2, 11); mmc3_set(3, 12); mmc3_set(4, 13); mmc3_set(5, 14)
    expect_chr(0, 4, "MMC3 R0"); expect_chr(1, 5, "MMC3 R0+1"); expect_chr(2, 6, "MMC3 R1"); expect_chr(3, 7, "MMC3 R1+1")
    expect_chr(4, 11, "MMC3 R2"); expect_chr(5, 12, "MMC3 R3"); expect_chr(6, 13, "MMC3 R4"); expect_chr(7, 14, "MMC3 R5")
    w(0x8000, 0x80 | 0); w(0x8001, 20)   -- inverted: R0 at $1000
    expect_chr(4, 20, "MMC3 inv R0"); expect_chr(0, 11, "MMC3 inv R2")
    mmc3_set(0x80 | 1, 7)                -- odd 2K bank: low bit ignored
    expect_chr(6, 6, "MMC3 R1 odd->even")
  end
end

tests[206] = function()
  local last = nprg8 - 1
  mmc3_set(6, 2); mmc3_set(7, 7)
  expect_prg(0, 2, "N108 R6"); expect_prg(1, 7, "N108 R7"); expect_prg(2, last - 1, "N108 fixed"); expect_prg(3, last, "N108 fixed")
  mmc3_set(0, 2); mmc3_set(1, 4); mmc3_set(2, 9); mmc3_set(5, 15)
  expect_chr(0, 2, "N108 R0"); expect_chr(2, 4, "N108 R1"); expect_chr(4, 9, "N108 R2"); expect_chr(7, 15, "N108 R5")
end

T.every_frame(function()
  local now = T.now()
  if phase == 0 then
    -- the image is in place once the loader has left and the stub idles at $8004
    local pc_hi = cpu:readv_u8(0xFFFD)
    if pc_hi == 0x80 and stamp_at(cpu, 0x8000, "P") ~= nil and now > 1.0 then
      phase = 1
    elseif now > timeout then
      phase = 9
      T.verdict(false, "image never landed")
    end
  elseif phase == 1 then
    phase = 9
    local t = tests[mapper]
    if not t then T.verdict(false, "no test for mapper " .. tostring(mapper)) return end
    t()
    T.verdict(fails == 0, string.format("mapper %d: %d checks, %d failures", mapper, checks, fails))
  end
end)
