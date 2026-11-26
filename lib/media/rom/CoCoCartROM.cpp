#include "CoCoCartROM.h"

#include <bit>

// CoCo cartridge images are 4K, 8K, 16K, 32K, 64K or 128K.
constexpr size_t COCO_CART_4K = 4_kB;
constexpr size_t COCO_CART_32K = 32_kB;
constexpr size_t COCO_CART_128K = 128_kB;

error_is_true CoCoCartROM::attach()
{
    if (!matchExtension(_path, extensions).has_value())
        RETURN_ERROR_AS_TRUE();

    if (readSize().is_error())
        RETURN_ERROR_AS_TRUE();

    if (_size < COCO_CART_4K || _size > COCO_CART_128K || !std::has_single_bit(_size))
        RETURN_ERROR_AS_TRUE();

    RETURN_SUCCESS_AS_FALSE();
}

std::optional<MapperLayout> CoCoCartROM::mapperLayout()
{
    // A14 is inverted, which puts the second 16K at $8000. Windows take banks in
    // address order, so its window sits above $FFFF and is mirrored down.
    if (_size <= COCO_CART_32K)
        return MapperLayout{{{0, 0, 0xC000, 0x10000}, {0, 0, 0x10000, 0x14000}},
                            {{0x8000, 0xC000, 0x10000}}};

    // GMC decodes its bank register at every even address in $FF40-$FF5F, since
    // the odd ones are its sound chip. Carts only write $FF40, so watch only that.
    return MapperLayout{{{0xFF40, 0xFF41, 0xC000, 0x10000}}, {{0x8000, 0xC000, 0xC000}}};
}
