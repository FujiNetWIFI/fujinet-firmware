#pragma once

#include <memory_resource>

#ifdef ESP_PLATFORM
#include <esp_heap_caps.h>
#include "sdkconfig.h"

// A std::pmr::memory_resource with PSRAMAllocator's policy: PSRAM first, the
// default heap when there is none or it is full.
class PSRAMMemoryResource : public std::pmr::memory_resource
{
    void *do_allocate(std::size_t bytes, std::size_t align) override
    {
#if CONFIG_SPIRAM
        if (void *p = heap_caps_aligned_alloc(align, bytes, MALLOC_CAP_SPIRAM))
            return p;
#endif
        return heap_caps_aligned_alloc(align, bytes, MALLOC_CAP_DEFAULT);
    }

    void do_deallocate(void *p, std::size_t, std::size_t) override { heap_caps_free(p); }

    bool do_is_equal(const std::pmr::memory_resource &o) const noexcept override
    {
        return this == &o;
    }
};
#endif

// PSRAM on the ESP32, the default heap elsewhere.
inline std::pmr::memory_resource *psram_memory_resource()
{
#ifdef ESP_PLATFORM
    static PSRAMMemoryResource psram;
    return &psram;
#else
    return std::pmr::get_default_resource();
#endif
}
