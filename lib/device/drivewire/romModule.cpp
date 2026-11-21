#ifdef BUILD_COCO

#include "romModule.h"

#include <algorithm>
#include <cstring>

#include "FujiDiskDrive.h"
#include "fujiHost.h"

#include "../../include/debug.h"

// Pages are read in chunks this size so no buffer of a page's size is needed in DRAM
static constexpr size_t CHUNK_SIZE = 2_kB;

std::unique_ptr<drivewireROMModule> drivewireROMModule::create(std::unique_ptr<ROMMedia> rom,
                                                               fujiHost &host,
                                                               const std::string &path)
{
    // The resource holds no state, so the object's own can free this memory
    PSRAMMemoryResource memory;
    auto *pageBuffer = static_cast<uint8_t *>(memory.allocate(pageSize(), 1));
    if (pageBuffer == nullptr)
    {
        rom->close();
        return nullptr;
    }
    return std::unique_ptr<drivewireROMModule>(
        new drivewireROMModule(std::move(rom), host, path, pageBuffer));
}

drivewireROMModule::~drivewireROMModule()
{
    closeFile();
    _memory.deallocate(_page, pageSize(), 1);
}

void drivewireROMModule::closeFile()
{
    if (_rom)
        _rom->close();
    _rom = nullptr;
    _next = 0;
}

error_is_true drivewireROMModule::rewind()
{
    closeFile();

    char fullpath[MAX_FILENAME_LEN];
    snprintf(fullpath, sizeof(fullpath), "%s", _path.c_str());
    fnFile *file = _host.fnfile_open(_path.c_str(), fullpath, sizeof(fullpath), "rb");
    if (file == nullptr)
        RETURN_ERROR_AS_TRUE();

    _rom = Formats::open<ROMMedia>(_path, file, &_host);
    if (!_rom)
    {
        fnio::fclose(file);
        RETURN_ERROR_AS_TRUE();
    }
    RETURN_SUCCESS_AS_FALSE();
}

error_is_true drivewireROMModule::readNext(uint8_t *pageBuffer)
{
    size_t offset = static_cast<size_t>(_next) * pageSize();
    size_t wanted = std::min(pageSize(), _size - offset);
    ByteBuffer chunk;

    for (size_t done = 0; done < wanted;)
    {
        auto got = _rom->read(chunk, std::min(CHUNK_SIZE, wanted - done));
        if (got.is_error() || got == 0)
            RETURN_ERROR_AS_TRUE();
        if (pageBuffer != nullptr)
            memcpy(pageBuffer + done, chunk.data(), got);
        done += got;
    }

    if (pageBuffer != nullptr)
        memset(pageBuffer + wanted, 0xFF, pageSize() - wanted);
    _next++;
    RETURN_SUCCESS_AS_FALSE();
}

drivewireROMModule::PageStatus drivewireROMModule::load(uint32_t pageNumber)
{
    size_t pages = (_size + pageSize() - 1) / pageSize();
    if (pageNumber >= pages)
        return PageStatus::PAST_END;

    if (pageNumber == _loaded)
        return PageStatus::OK;
    _loaded = UINT32_MAX;

    // The file can only be read forward, and is closed after a failed read
    if ((!_rom || pageNumber < _next) && rewind().is_error())
        return PageStatus::READ_ERROR;

    while (_next < pageNumber)
    {
        if (readNext(nullptr).is_error())
        {
            closeFile();
            return PageStatus::READ_ERROR;
        }
    }

    if (readNext(_page).is_error())
    {
        closeFile();
        return PageStatus::READ_ERROR;
    }

    _loaded = pageNumber;
    return PageStatus::OK;
}

#endif /* BUILD_COCO */
