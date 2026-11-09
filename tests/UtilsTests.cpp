#define DOCTEST_CONFIG_IMPLEMENT_WITH_MAIN
#include <doctest/doctest.h>

#include "utils.h"
#include "samlib.h"
#include "string_utils.h"
#include "U8Char.h"

int sam(int argc, char **argv) { return 0; }

TEST_CASE("util_stoi parses what std::stoi parses")
{
    CHECK(util_stoi("23") == 23);
    CHECK(util_stoi(" 1701") == 1701);
    CHECK(util_stoi("-1") == -1);
    CHECK(util_stoi("8080/") == 8080);
}

TEST_CASE("util_stoi returns nullopt where std::stoi throws")
{
    CHECK_FALSE(util_stoi("").has_value());
    CHECK_FALSE(util_stoi("//10.4.0.19:1701").has_value());
    CHECK_FALSE(util_stoi("abc").has_value());
    CHECK_FALSE(util_stoi("99999999999999999999").has_value());
}

TEST_CASE("mstr::format prints every argument")
{
    // It measures the text before printing it; both need the arguments
    CHECK(mstr::format("%.2f %s", 63.99, "K") == "63.99 K");
    CHECK(mstr::format("%.16s|%.5s|%d", "FUJINET DISK", "FN 2A", 42) == "FUJINET DISK|FN 2A|42");
}

TEST_CASE("mstr::formatBytes")
{
    CHECK(mstr::formatBytes(65535) == "64.00 K");
    CHECK(mstr::formatBytes(3 * 1024 * 1024) == "3.00 M");
}

TEST_CASE("mstr::toUTF8 converts PETSCII bytes from 0x80 up and drops NUL")
{
    const char shiftedA = static_cast<char>(0xC1);
    std::string expected = U8Char(shiftedA).toUtf8();
    REQUIRE_FALSE(expected.empty());
    CHECK(mstr::toUTF8(std::string(1, shiftedA)) == expected);
    CHECK(mstr::toUTF8(std::string("\0", 1)).empty());
}
