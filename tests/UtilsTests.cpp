#define DOCTEST_CONFIG_IMPLEMENT_WITH_MAIN
#include <doctest/doctest.h>

#include "utils.h"
#include "samlib.h"

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
