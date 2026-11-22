#include "CoCoCartROM.h"

#include <bit>

// CoCo cartridge images are 4K, 8K, 16K, 32K, 64K or 128K.
constexpr size_t COCO_CART_4K = 4_kB;
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
