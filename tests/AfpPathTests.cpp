#define DOCTEST_CONFIG_IMPLEMENT_WITH_MAIN
#include <doctest/doctest.h>

#include "afpPath.h"

TEST_CASE("a file inside a volume")
{
    AfpPath p = afp_split_path("/meatloaf/apps/readme.txt");
    CHECK(p.volume == "meatloaf");
    CHECK(p.path == "/apps/readme.txt");
}

TEST_CASE("the volume root, with and without a trailing slash")
{
    AfpPath bare = afp_split_path("/meatloaf");
    CHECK(bare.volume == "meatloaf");
    CHECK(bare.path == "/");

    AfpPath slash = afp_split_path("/meatloaf/");
    CHECK(slash.volume == "meatloaf");
    CHECK(slash.path == "/");
}

TEST_CASE("no volume at all")
{
    for (const char *in : {"", "/", "//"})
    {
        CAPTURE(in);
        AfpPath p = afp_split_path(in);
        CHECK(p.volume.empty());
        CHECK(p.path.empty());
    }
}

TEST_CASE("a directory keeps no trailing slash")
{
    AfpPath p = afp_split_path("/meatloaf/apps/games/");
    CHECK(p.volume == "meatloaf");
    CHECK(p.path == "/apps/games");
}

TEST_CASE("leading slashes are optional and may repeat")
{
    AfpPath none = afp_split_path("meatloaf/apps");
    CHECK(none.volume == "meatloaf");
    CHECK(none.path == "/apps");

    AfpPath two = afp_split_path("//meatloaf/apps");
    CHECK(two.volume == "meatloaf");
    CHECK(two.path == "/apps");
}

TEST_CASE("names with spaces and dots pass through")
{
    AfpPath p = afp_split_path("/My Volume/Some Folder/a.b.c");
    CHECK(p.volume == "My Volume");
    CHECK(p.path == "/Some Folder/a.b.c");
}
