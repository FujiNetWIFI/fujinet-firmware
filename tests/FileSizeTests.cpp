#define DOCTEST_CONFIG_IMPLEMENT_WITH_MAIN
#include <doctest/doctest.h>

#include <cstdint>
#include <type_traits>

#include "global_types.h"

static_assert(std::is_convertible_v<file_size_t, ssize_t>);
static_assert(std::is_convertible_v<file_size_t, long long>);
static_assert(!std::is_convertible_v<file_size_t, uint32_t>);
static_assert(!std::is_convertible_v<file_size_t, size_t>);
static_assert(!std::is_convertible_v<file_size_t, int8_t>);
static_assert(!std::is_convertible_v<file_size_t, bool>);
static_assert(!std::is_convertible_v<file_size_t, double>);
static_assert(!std::is_convertible_v<ssize_t, file_size_t>);

TEST_CASE("a file size compares as the signed number it holds")
{
    CHECK(file_size_t(-1) < 0);
    CHECK(file_size_t(-1) == -1);
    CHECK(file_size_t(0) >= 0);
    CHECK(file_size_t(512) > 511u);
    CHECK_FALSE(file_size_t(-1) > 0u);
}

TEST_CASE("to() refuses -1 and a size the target can't hold, and leaves it untouched")
{
    uint32_t u32 = 7;
    CHECK(file_size_t(-1).to(&u32).is_error());
    CHECK(u32 == 7);
    CHECK(file_size_t(1000).to(&u32).is_success());
    CHECK(u32 == 1000);

    uint16_t u16 = 7;
    CHECK(file_size_t(70000).to(&u16).is_error());
    CHECK(u16 == 7);
    CHECK(file_size_t(65535).to(&u16).is_success());
    CHECK(u16 == 65535);

    if constexpr (sizeof(ssize_t) > sizeof(uint32_t))
    {
        CHECK(file_size_t(static_cast<ssize_t>(UINT32_MAX) + 1).to(&u32).is_error());
        CHECK(file_size_t(UINT32_MAX).to(&u32).is_success());
    }
}
