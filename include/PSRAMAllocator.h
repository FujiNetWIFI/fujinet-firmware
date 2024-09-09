/// \copyright
/// Copyright 2021 Mike Dunston (https://github.com/atanisoft)
///
/// Licensed under the Apache License, Version 2.0 (the "License");
/// you may not use this file except in compliance with the License.
/// You may obtain a copy of the License at
///
///     http://www.apache.org/licenses/LICENSE-2.0
///
/// Unless required by applicable law or agreed to in writing, software
/// distributed under the License is distributed on an "AS IS" BASIS,
/// WITHOUT WARRANTIES OR CONDITIONS OF ANY KIND, either express or implied.
/// See the License for the specific language governing permissions and
/// limitations under the License.
///
/// \file psram_allocator.h
/// This file declares an allocator that provides memory from PSRAM rather than
/// internal memory.

#pragma once

#include <esp_heap_caps.h>
#include "sdkconfig.h"

template <class T>
class PSRAMAllocator
{
public:
    using value_type = T;

    PSRAMAllocator() noexcept
    {
    }

    template <class U> constexpr PSRAMAllocator(const PSRAMAllocator<U>&) noexcept
    {
    }

    [[nodiscard]] value_type* allocate(std::size_t n)
    {
#if CONFIG_SPIRAM
        // attempt to allocate in PSRAM first
        auto p = static_cast<value_type*>(heap_caps_malloc(n * sizeof(value_type), MALLOC_CAP_SPIRAM));
        if (p)
        {
            return p;
        }
#endif // CONFIG_SPIRAM

        // If the allocation in PSRAM failed (or PSRAM not enabled), try to
        // allocate from the default memory pool.
        auto p2 = static_cast<value_type*>(heap_caps_malloc(n * sizeof(value_type), MALLOC_CAP_DEFAULT));
        if (p2)
        {
            return p2;
        }

        return NULL;
    }

    void deallocate(value_type* p, std::size_t) noexcept
    {
        heap_caps_free(p);
    }
};
template <class T, class U>
bool operator==(const PSRAMAllocator<T>&, const PSRAMAllocator<U>&)
{
    return true;
}
template <class T, class U>
bool operator!=(const PSRAMAllocator<T>& x, const PSRAMAllocator<U>& y)
{
    return !(x == y);
}

#include <memory> // For std::unique_ptr

// Custom deleter
template <class T>
struct PSRAMDeleter
{
    void operator()(T* ptr) const
    {
        if (ptr) {
            ptr->~T(); // Call the destructor manually for the object
            PSRAMAllocator<T> allocator;
            allocator.deallocate(ptr, 1);
        }
    }
};