#include "IMDDisk.h"

#include <algorithm>
#include <ctime>

error_is_true IMDDisk::attach()
{
    if (!matchExtension(_path, extensions).has_value()
        && (matchExtension(_path, rawExtensions).has_value()
            || _image.probe() != IMDStatus::Ok))
        RETURN_ERROR_AS_TRUE();

    if (_image.open(!_readOnly) != IMDStatus::Ok || _image.lbaCount() == 0)
        RETURN_ERROR_AS_TRUE();

    // Max + 1 rather than a distinct count, so tracks missing from the image
    // don't shrink the geometry.
    uint8_t maxCylinder = 0;
    uint8_t maxHead = 0;
    IMDTrackInfo info;
    for (uint32_t track = 0; track < _image.trackCount(); track++)
    {
        if (_image.trackInfo(track, info) != IMDStatus::Ok)
            continue;
        maxCylinder = std::max(maxCylinder, info.cyl);
        maxHead = std::max(maxHead, info.head);
    }
    _cylinders = maxCylinder + 1;
    _sides = maxHead + 1;
    RETURN_SUCCESS_AS_FALSE();
}

TrackGeometry IMDDisk::trackGeometry(uint16_t cylinder, uint8_t side) const
{
    IMDTrackInfo info;
    for (uint32_t track = 0; track < _image.trackCount(); track++)
    {
        if (_image.trackInfo(track, info) != IMDStatus::Ok || info.cyl != cylinder
            || info.head != side)
            continue;
        return {static_cast<uint8_t>(info.nsec), _image.sectorSize(info.first_lba),
                IMDImage::modeIsMFM(info.mode) ? TrackEncoding::MFM : TrackEncoding::FM};
    }
    return {0, 0};
}

error_is_true IMDDisk::readBlock(uint32_t block, ByteBuffer &buffer)
{
    uint16_t size = blockSize(block);
    if (size == 0)
        RETURN_ERROR_AS_TRUE();

    buffer.resize(size);
    uint16_t length = 0;
    if (_image.readSector(block, buffer.data(), size, &length) != IMDStatus::Ok)
        RETURN_ERROR_AS_TRUE();
    buffer.resize(length);
    RETURN_SUCCESS_AS_FALSE();
}

error_is_true IMDDisk::writeBlock(uint32_t block, const ByteBuffer &buffer)
{
    uint16_t size = blockSize(block);
    if (_readOnly || size == 0 || buffer.size() != size)
        RETURN_ERROR_AS_TRUE();
    RETURN_ERROR_IF(_image.writeSector(block, buffer.data(), size) != IMDStatus::Ok);
}

error_is_true IMDDisk::close()
{
    _image.close();
    return BlockMedia::close();
}

success_is_true IMDDisk::create(fnFile *f, uint16_t sectorSize, uint32_t numSectors)
{
    const uint8_t cylinders = 77, sectors = 26;
    // As mkfs.cpm -f ibm-3740 leaves it: E5 through the directory on track 2
    const uint8_t blankCylinders = 3;

    if (sectorSize != 128 || numSectors != cylinders * sectors)
        RETURN_ERROR_AS_FALSE();

    IMDImage image(f);
    if (image.writeHeader(time(nullptr)) != IMDStatus::Ok)
        RETURN_ERROR_AS_FALSE();
    for (uint8_t cylinder = 0; cylinder < cylinders; cylinder++)
    {
        // Mode 0 is 500 kbps FM; size code 0 is 128 bytes
        if (image.writeBlankTrack(0, cylinder, 0, sectors, 0,
                                  cylinder < blankCylinders ? 0xE5 : 0x00)
            != IMDStatus::Ok)
            RETURN_ERROR_AS_FALSE();
    }
    RETURN_SUCCESS_AS_TRUE();
}
