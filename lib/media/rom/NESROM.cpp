#include "NESROM.h"

#include <cstring>

constexpr size_t INES_HEADER_SIZE = 16;
constexpr size_t INES_TRAINER_SIZE = 512;
constexpr uint32_t INES_PRG_UNIT = 16_kB;
constexpr uint32_t INES_CHR_UNIT = 8_kB;

// A NES 2.0 ROM size: a 12-bit count of units, or when its top nibble is all
// ones, an exponent and multiplier packed into the low byte.
static uint64_t nes2_rom_size(uint8_t lsb, uint8_t msbNibble, uint32_t unit)
{
    if (msbNibble == 0x0F)
        return (uint64_t(1) << (lsb >> 2)) * ((lsb & 0x03) * 2 + 1);
    return uint64_t((msbNibble << 8) | lsb) * unit;
}

// A .nes recognized by the iNES "NES\x1A" header, and long enough to hold the
// trainer, PRG ROM, and CHR ROM the header says it has.
error_is_true NESROM::attach()
{
    if (!matchExtension(_path, {".nes"}).has_value())
        RETURN_ERROR_AS_TRUE();

    uint8_t header[INES_HEADER_SIZE];
    if (fnio::fread(header, 1, sizeof(header), _fileh) != sizeof(header)
        || memcmp(header, "NES\x1A", 4) != 0)
        RETURN_ERROR_AS_TRUE();

    if (readSize().is_error())
        RETURN_ERROR_AS_TRUE();

    bool isNes2 = (header[7] & 0x0C) == 0x08;
    uint64_t prgSize = isNes2 ? nes2_rom_size(header[4], header[9] & 0x0F, INES_PRG_UNIT)
                              : uint64_t(header[4]) * INES_PRG_UNIT;
    uint64_t chrSize = isNes2 ? nes2_rom_size(header[5], header[9] >> 4, INES_CHR_UNIT)
                              : uint64_t(header[5]) * INES_CHR_UNIT;
    uint64_t trainerSize = (header[6] & 0x04) ? INES_TRAINER_SIZE : 0;

    RETURN_ERROR_IF(prgSize == 0
                    || _size < INES_HEADER_SIZE + trainerSize + prgSize + chrSize);
}
