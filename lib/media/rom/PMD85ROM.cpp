#include "PMD85ROM.h"

#include <algorithm>
#include <cstring>

#include "../../include/PSRAMMemoryResource.h"

// Stateless: it only decides where the page goes, PSRAM first on the ESP32
static PSRAMMemoryResource psram;

void PMD85ROM::PageFree::operator()(uint8_t *ptr) const
{
    psram.deallocate(ptr, PAGE_SIZE, 1);
}

error_is_true PMD85ROM::attach()
{
    if (!matchExtension(_path, {".rmm", ".mrm"}).has_value())
        RETURN_ERROR_AS_TRUE();

    if (readSize().is_error() || _size == 0 || _size > PAGE_SIZE * MAX_PAGES)
        RETURN_ERROR_AS_TRUE();

    _page.reset(static_cast<uint8_t *>(psram.allocate(PAGE_SIZE, 1)));
    if (!_page)
        RETURN_ERROR_AS_TRUE();

    RETURN_SUCCESS_AS_FALSE();
}

error_is_true PMD85ROM::readPage(uint32_t page)
{
    if (page >= pageCount() || _fileh == nullptr)
        RETURN_ERROR_AS_TRUE();

    if (page == _loaded)
        RETURN_SUCCESS_AS_FALSE();
    _loaded = UINT32_MAX;

    size_t offset = page * PAGE_SIZE;
    size_t count = std::min(PAGE_SIZE, _size - offset);
    std::memset(_page.get(), 0xFF, PAGE_SIZE);
    if (fnio::fseek(_fileh, offset, SEEK_SET) != 0 ||
        fnio::fread(_page.get(), 1, count, _fileh) != count)
        RETURN_ERROR_AS_TRUE();

    _loaded = page;
    RETURN_SUCCESS_AS_FALSE();
}
