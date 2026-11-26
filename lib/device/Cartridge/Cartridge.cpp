#include "Cartridge.h"

#include <algorithm>

#include "../../include/debug.h"

namespace
{

constexpr uint16_t ROM_IMAGE_SEGMENT = 0;

// Legacy: the game console carts don't read packet parameters, so these values
// go in OPEN's data for them.
struct ConsoleCartOpenData
{
    bool isMapperInfo;
    u32le_t size;
};
static_assert(sizeof(ConsoleCartOpenData) == 5, "ConsoleCartOpenData must not be padded");

ByteBuffer consoleCartOpenData(bool isMapperInfo, uint32_t size)
{
    ConsoleCartOpenData openData;
    openData.isMapperInfo = isMapperInfo;
    openData.size = size;
    const uint8_t *bytes = reinterpret_cast<const uint8_t *>(&openData);
    return ByteBuffer(bytes, bytes + sizeof(openData));
}

} // namespace

// Legacy: the game console carts activate the ROM on the image's CLOSE, so .cfg
// mapper info goes first.
error_is_true Cartridge::mount(ROMMedia &rom)
{
    error_is_true pushed(false);
    auto info = rom.mapperInfo();
    if (info.has_value())
        pushed = pushMapperInfo(*info);
    if (pushed.is_success())
        pushed = pushImage(rom);
    rom.close();
    return pushed;
}

// Legacy: the game console carts expect .cfg mapper info as a separate transfer
// with no segment.
error_is_true Cartridge::pushMapperInfo(const std::pmr::vector<uint8_t> &info)
{
    if (info.empty())
    {
        Debug_printv("ROM push: mapper info is empty\n");
        RETURN_ERROR_AS_TRUE();
    }

    if (send(CMD::DBC_SEGMENT_OPEN, consoleCartOpenData(true, info.size())).is_error())
    {
        Debug_printv("ROM push: failed to open mapper info (%lu bytes)\n",
                     static_cast<unsigned long>(info.size()));
        RETURN_ERROR_AS_TRUE();
    }

    error_is_true pushed(false);
    for (size_t offset = 0; offset < info.size(); offset += BLOCK_SIZE)
    {
        size_t count = std::min(BLOCK_SIZE, info.size() - offset);
        pushed = send(CMD::DBC_SEGMENT_WRITE,
                      ByteBuffer(info.begin() + offset, info.begin() + offset + count));
        if (pushed.is_error())
        {
            Debug_printv("ROM push: failed to send mapper info block\n");
            break;
        }
    }

    return closeSegment(pushed);
}

error_is_true Cartridge::pushImage(ROMMedia &rom)
{
    uint32_t size = rom.size();
    if (size == 0)
    {
        Debug_printv("ROM push: image is empty or unreadable\n");
        RETURN_ERROR_AS_TRUE();
    }

    if (send(CMD::DBC_SEGMENT_OPEN, ROM_IMAGE_SEGMENT, consoleCartOpenData(false, size))
            .is_error())
    {
        Debug_printv("ROM push: failed to open image segment (%lu bytes)\n",
                     static_cast<unsigned long>(size));
        RETURN_ERROR_AS_TRUE();
    }

    error_is_true pushed(false);
    ByteBuffer buffer;
    uint32_t sent = 0;
    while (sent < size)
    {
        auto got = rom.read(buffer, BLOCK_SIZE);
        if (got.is_error() || buffer.empty())
            break; // the size check below reports it

        pushed = send(CMD::DBC_SEGMENT_WRITE, buffer);
        if (pushed.is_error())
        {
            Debug_printv("ROM push: failed to send image block\n");
            break;
        }
        sent += buffer.size();
    }

    if (pushed.is_success() && sent != size)
    {
        Debug_printv("ROM push: image short transfer: %lu of %lu bytes\n",
                     static_cast<unsigned long>(sent), static_cast<unsigned long>(size));
        pushed = error_is_true(true);
    }

    auto layout = rom.mapperLayout();
    if (pushed.is_success() && layout.has_value())
        pushed = pushMapperLayout(*layout);

    return closeSegment(pushed);
}

// The cart uses the window size to count its banks at CLOSE, so the layout has
// to be sent first.
error_is_true Cartridge::pushMapperLayout(const MapperLayout &layout)
{
    if (send(CMD::DBC_MAPPER_SET_INFO).is_error())
    {
        Debug_printv("ROM push: failed to reset mapper\n");
        RETURN_ERROR_AS_TRUE();
    }

    for (const auto &window : layout.windows)
    {
        if (send(CMD::DBC_MAPPER_ADD_WINDOW, window.regBase, window.regTop,
                 window.windowBase, window.windowTop).is_error())
        {
            Debug_printv("ROM push: failed to add mapper window\n");
            RETURN_ERROR_AS_TRUE();
        }
    }

    for (const auto &mirror : layout.mirrors)
    {
        if (send(CMD::DBC_MAPPER_ADD_MIRROR, mirror.mirrorBase, mirror.mirrorTop,
                 mirror.source).is_error())
        {
            Debug_printv("ROM push: failed to add mapper mirror\n");
            RETURN_ERROR_AS_TRUE();
        }
    }
    RETURN_SUCCESS_AS_FALSE();
}

// CLOSE always goes out, so a later WRITE can't land in a segment left open.
// Legacy: after a failure its 0x01 data byte keeps a game console cart from
// activating the ROM.
error_is_true Cartridge::closeSegment(error_is_true pushed)
{
    error_is_true closed = pushed.is_success()
        ? send(CMD::DBC_SEGMENT_CLOSE)
        : send(CMD::DBC_SEGMENT_CLOSE, ByteBuffer{0x01});
    if (closed.is_error())
    {
        Debug_printv("ROM push: close failed/rejected\n");
        RETURN_ERROR_AS_TRUE();
    }
    return pushed;
}
