#define DOCTEST_CONFIG_IMPLEMENT_WITH_MAIN
#include <doctest/doctest.h>

#include "peoples_url_parser.h"
#include "samlib.h"

// utils.cpp's speech helper is never called here; the stub keeps SAM out of the link.
int sam(int argc, char **argv) { return 0; }

TEST_CASE("'?' and '#' start the query and fragment in path")
{
    PeoplesUrlParser p("HTTP://host/page.html?a=1#top");
    CHECK(p.path == "/page.html");
    CHECK(p.query == "a=1");
    CHECK(p.fragment == "top");
}

TEST_CASE("getFilePath keeps '#' and '?' in the file name")
{
    PeoplesUrlParser p("TNFS://host/dir/Disk #2.atr");
    CHECK(p.host == "host");
    CHECK(p.getFilePath() == "/dir/Disk #2.atr");

    PeoplesUrlParser q("SMB://user:pw@host:445/share/What?.txt");
    CHECK(q.user == "user");
    CHECK(q.port == "445");
    CHECK(q.getFilePath() == "/share/What?.txt");
}

TEST_CASE("getFilePath decodes %XX escapes")
{
    CHECK(PeoplesUrlParser("TNFS://host/a%23b%20c.atr").getFilePath() == "/a#b c.atr");
    CHECK(PeoplesUrlParser("TNFS://host/a%2fb").getFilePath() == "/a/b");
    CHECK(PeoplesUrlParser("TNFS://host/a%23b").getFilePath(false) == "/a%23b");

    // Malformed or %00 escapes are left as they are.
    CHECK(PeoplesUrlParser("TNFS://host/100%").getFilePath() == "/100%");
    CHECK(PeoplesUrlParser("TNFS://host/a%zzb").getFilePath() == "/a%zzb");
    CHECK(PeoplesUrlParser("TNFS://host/a%00b").getFilePath() == "/a%00b");
}

TEST_CASE("getFilePath is canonical like path")
{
    CHECK(PeoplesUrlParser("TNFS://host/a/../b#1/./c.atr").getFilePath() == "/b#1/c.atr");
    CHECK(PeoplesUrlParser("TNFS://host/dir/").getFilePath() == "/dir/");
    CHECK(PeoplesUrlParser("TNFS://host").getFilePath() == "/");

    PeoplesUrlParser p("TNFS://host/dir/plain.atr");
    CHECK(p.getFilePath() == p.path);
}
