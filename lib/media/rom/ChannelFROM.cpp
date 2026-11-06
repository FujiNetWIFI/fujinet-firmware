#include "ChannelFROM.h"

// The BIOS runs its built-in game instead of the cartridge unless the image
// starts with $55.
constexpr uint8_t CHANNELF_CART_SIGNATURE = 0x55;

error_is_true ChannelFROM::attach()
{
    if (!matchExtension(_path, {".chf"}).has_value())
        RETURN_ERROR_AS_TRUE();

    uint8_t signature;
    if (fnio::fread(&signature, 1, 1, _fileh) != 1 || signature != CHANNELF_CART_SIGNATURE)
        RETURN_ERROR_AS_TRUE();

    return readSize();
}
