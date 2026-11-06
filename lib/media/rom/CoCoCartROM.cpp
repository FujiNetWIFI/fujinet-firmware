#include "CoCoCartROM.h"

#include <bit>

// CoCo cartridge images are 8K, 16K, 32K, 64K or 128K.
constexpr size_t COCO_CART_8K = 8_kB;
constexpr size_t COCO_CART_128K = 128_kB;

error_is_true CoCoCartROM::attach()
{
    if (!matchExtension(_path, {".ccc", ".rom"}).has_value())
        RETURN_ERROR_AS_TRUE();

    if (readSize().is_error())
        RETURN_ERROR_AS_TRUE();

    if (_size < COCO_CART_8K || _size > COCO_CART_128K || _size % COCO_CART_8K != 0)
        RETURN_ERROR_AS_TRUE();

    // Carts come in 1, 2, 4, 8 or 16 banks of 8K
    size_t banks = _size / COCO_CART_8K;
    if (!std::has_single_bit(banks))
        RETURN_ERROR_AS_TRUE();

    RETURN_SUCCESS_AS_FALSE();
}
