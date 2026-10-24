#ifndef PSRAM_MEMORY_RESOURCE_H
#define PSRAM_MEMORY_RESOURCE_H

// A std::pmr::memory_resource for bulk tables: PSRAM first, then ordinary memory,
// as PSRAMAllocator does. Off the ESP32 it is ordinary memory.

#include <memory_resource>

#ifdef ESP_PLATFORM
#include <esp_heap_caps.h>
#include "sdkconfig.h"
#endif

class PSRAMMemoryResource : public std::pmr::memory_resource
{
protected:
    // Exceptions are off in the firmware, so a failed allocation returns null rather
    // than throwing, as PSRAMAllocator does
    void *do_allocate(size_t bytes, size_t alignment) override
    {
#ifdef ESP_PLATFORM
#if CONFIG_SPIRAM
        void *ptr = heap_caps_aligned_alloc(alignment, bytes, MALLOC_CAP_SPIRAM);
        if (ptr != nullptr)
            return ptr;
#endif
        return heap_caps_aligned_alloc(alignment, bytes, MALLOC_CAP_DEFAULT);
#else
        return std::pmr::new_delete_resource()->allocate(bytes, alignment);
#endif
    }

    void do_deallocate(void *ptr, size_t bytes, size_t alignment) override
    {
#ifdef ESP_PLATFORM
        (void)bytes;
        (void)alignment;
        heap_caps_free(ptr);
#else
        std::pmr::new_delete_resource()->deallocate(ptr, bytes, alignment);
#endif
    }

    bool do_is_equal(const std::pmr::memory_resource &other) const noexcept override
    {
        return this == &other;
    }
};

#endif // PSRAM_MEMORY_RESOURCE_H
