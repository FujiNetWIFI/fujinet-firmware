#define DOCTEST_CONFIG_IMPLEMENT_WITH_MAIN
#include <doctest/doctest.h>

#include "peoples_url_parser.h"
#include "samlib.h"

// utils.cpp's speech helper is never called here; the stub keeps SAM out of the link.
int sam(int argc, char **argv) { return 0; }

static std::unique_ptr<PeoplesUrlParser> whole(const std::string &u)
{
    auto p = PeoplesUrlParser::parseURL(u);
    p->keepWholePath();
    return p;
}

TEST_CASE("by default '?' and '#' start the query and fragment")
{
    auto p = PeoplesUrlParser::parseURL("HTTP://host/page.html?a=1#top");
    CHECK(p->path == "/page.html");
    CHECK(p->query == "a=1");
    CHECK(p->fragment == "top");
}

TEST_CASE("keepWholePath leaves '#' and '?' in the file name")
{
    auto p = whole("TNFS://host/dir/Disk #2.atr");
    CHECK(p->host == "host");
    CHECK(p->path == "/dir/Disk #2.atr");
    CHECK(p->name == "Disk #2.atr");
    CHECK(p->base_name == "Disk #2");
    CHECK(p->extension == "atr");
    CHECK(p->pathToFile() == "/dir/");
    CHECK(p->fragment.empty());

    p = whole("SMB://user:pw@host:445/share/What?.txt");
    CHECK(p->user == "user");
    CHECK(p->port == "445");
    CHECK(p->path == "/share/What?.txt");
    CHECK(p->query.empty());
}

TEST_CASE("keepWholePath decodes %XX escapes")
{
    CHECK(whole("TNFS://host/a%23b%20c.atr")->path == "/a#b c.atr");
    CHECK(whole("TNFS://host/a%2fb")->path == "/a/b");

    // Malformed or %00 escapes are left as they are.
    CHECK(whole("TNFS://host/100%")->path == "/100%");
    CHECK(whole("TNFS://host/a%zzb")->path == "/a%zzb");
    CHECK(whole("TNFS://host/a%00b")->path == "/a%00b");
}

TEST_CASE("keepWholePath keeps the path canonical")
{
    CHECK(whole("TNFS://host/a/../b#1/./c.atr")->path == "/b#1/c.atr");
    CHECK(whole("TNFS://host/dir/")->path == "/dir/");
}
