#include "StudioIIROM.h"

#include <cstring>

constexpr size_t ST2_BLOCK_SIZE = 256;
constexpr uint8_t ST2_MIN_BLOCKS = 2;
// Without a header the image goes straight into the 1K cartridge window at $0400
constexpr size_t ST2_RAW_MAX = 1024;

// The RCA2 header is optional. One that is there counts the 256-byte blocks in
// the file, the header block included, so there must be at least one block of
// data and the file must hold all of them. Without one, the image has nowhere
// to go but the cartridge window, so it must fit in it.
error_is_true StudioIIROM::attach()
{
    if (!matchExtension(_path, {".st2"}).has_value())
        RETURN_ERROR_AS_TRUE();

    uint8_t header[5];
    size_t got = fnio::fread(header, 1, sizeof(header), _fileh);
    bool hasHeader = got == sizeof(header) && memcmp(header, "RCA2", 4) == 0;

    if (readSize().is_error() || _size == 0)
        RETURN_ERROR_AS_TRUE();

    if (!hasHeader)
        RETURN_ERROR_IF(_size > ST2_RAW_MAX);

    uint8_t blocks = header[4];
    RETURN_ERROR_IF(blocks < ST2_MIN_BLOCKS || _size < blocks * ST2_BLOCK_SIZE);
}
