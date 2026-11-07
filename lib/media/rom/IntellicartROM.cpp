#include "IntellicartROM.h"

constexpr uint8_t INTELLICART_MAGIC = 0xA8;

// A .rom recognized by the Intellicart header: 0xA8, then a segment count
// followed by its complement.
error_is_true IntellicartROM::attach()
{
    if (!matchExtension(_path, {".rom"}).has_value())
        RETURN_ERROR_AS_TRUE();

    uint8_t header[3];
    if (fnio::fread(header, 1, sizeof(header), _fileh) != sizeof(header))
        RETURN_ERROR_AS_TRUE();

    uint8_t segments = header[1];
    if (header[0] != INTELLICART_MAGIC || header[2] != (uint8_t)(segments ^ 0xFF))
        RETURN_ERROR_AS_TRUE();

    return readSize();
}
