#include "Atari2600ROM.h"

#include <cstring>

// The sizes the cart picks a bankswitching scheme from; anything else it refuses.
constexpr size_t A2600_FIXED_SIZES[] = {
    2_kB, 4_kB, 8_kB,
    8_kB + 3,       // PP
    12_kB, 16_kB,
    29_kB,          // DPC+
    32_kB, 64_kB, 128_kB, 256_kB,
};

// DPC (Pitfall II): 8K of program and 2K of graphics, plus a few bytes
constexpr size_t A2600_DPC_MIN = 10_kB;
constexpr size_t A2600_DPC_MAX = 10_kB + 256;

// 3E+ banks in 1K units
constexpr size_t A2600_3EPLUS_UNIT = 1024;
constexpr size_t A2600_3EPLUS_MAX = 64_kB;

// ELF and ACE images carry a header and can be any size
constexpr char A2600_ELF_MAGIC[] = "\x7F" "ELF";
constexpr const char *A2600_ACE_MAGICS[] = {"ACE-UF00", "ACE-PC00", "ACE-UC00"};

// There's no header on a plain 2600 image, so its size is all there is to check:
// one of the sizes the cart maps, or an ELF or ACE image, which say what they are.
error_is_true Atari2600ROM::attach()
{
    if (!matchExtension(_path, {".bin", ".a26"}).has_value())
        RETURN_ERROR_AS_TRUE();

    char magic[8] = {};
    size_t got = fnio::fread(magic, 1, sizeof(magic), _fileh);

    if (readSize().is_error() || _size == 0)
        RETURN_ERROR_AS_TRUE();

    for (size_t fixedSize : A2600_FIXED_SIZES)
        if (_size == fixedSize)
            RETURN_SUCCESS_AS_FALSE();

    if (_size >= A2600_DPC_MIN && _size <= A2600_DPC_MAX)
        RETURN_SUCCESS_AS_FALSE();

    if (_size % A2600_3EPLUS_UNIT == 0 && _size <= A2600_3EPLUS_MAX)
        RETURN_SUCCESS_AS_FALSE();

    if (got >= 4 && memcmp(magic, A2600_ELF_MAGIC, 4) == 0)
        RETURN_SUCCESS_AS_FALSE();

    for (const char *aceMagic : A2600_ACE_MAGICS)
        if (got == sizeof(magic) && memcmp(magic, aceMagic, sizeof(magic)) == 0)
            RETURN_SUCCESS_AS_FALSE();

    RETURN_ERROR_AS_TRUE();
}
