#define DOCTEST_CONFIG_IMPLEMENT_WITH_MAIN
#include <doctest/doctest.h>

#include "ftp/ftp_dir_parse.h"

#include <sstream>
#include <string>
#include <vector>

// --------------------------------------------------------------------------------
// The entry loop behind NDIR over FTP (fujinet-firmware #1652).
//
// NetworkProtocolFS::open_dir() runs `while (read_dir_entry(...) == FUJI_ERROR::NONE)`,
// so the contract this file pins is narrow and unforgiving: NONE means "here is a
// real entry", anything else means "stop". Returning NONE with nothing to show is
// an infinite loop on hardware -- 100% CPU, no SIO response, Atari error 138 -- and
// returning not-NONE one entry early silently loses a file.
//
// ftp_dir_parse.cpp exists precisely so those edges can be driven from a string
// here rather than from a socket on a real FujiNet.
// --------------------------------------------------------------------------------

namespace {

struct Entry
{
    std::string name;
    long size;
    bool is_dir;
};

// Drain a listing the way open_dir() does, stopping the moment the parser says so.
// Caps out at `limit` entries so a regression that never terminates fails the test
// instead of hanging the suite -- which is the exact bug #1652 fixed.
std::vector<Entry> drain(const std::string &listing, size_t limit = 64)
{
    std::istringstream in(listing);
    std::vector<Entry> out;

    for (size_t i = 0; i < limit; ++i)
    {
        Entry e{"<untouched>", -1, false};
        if (ftp_next_dir_entry(in, e.name, e.size, e.is_dir) != FUJI_ERROR::NONE)
            break;
        out.push_back(e);
    }

    return out;
}

std::vector<std::string> names(const std::vector<Entry> &entries)
{
    std::vector<std::string> out;
    for (const auto &e : entries)
        out.push_back(e.name);
    return out;
}

// A UNIX ls listing, given the line ending the caller wants. Real servers send
// CRLF; some send bare LF, and the old code assumed CRLF unconditionally.
std::string unix_listing(const char *eol)
{
    return std::string("-rw-r--r--    1 ftp      ftp            26 May 03  2020 robots.txt") + eol
         + "drwxr-xr-x  658 ftp      ftp         16384 Apr 29  2025 users" + eol
         + "-rw-r--r--    1 ftp      ftp          1329 Dec 12  2007 welcome.msg" + eol;
}

// The real root of ftp.untergrund.net, the server in the #1652 bug report,
// captured verbatim. Note the symlink first and welcome.msg last.
const char *kUntergrundRoot =
    "lrwxrwxrwx    1 ftp      ftp            17 Feb 03  2012 breakpoint -> users/breakpoint/\r\n"
    "-rw-r--r--    1 ftp      ftp            26 May 03  2020 robots.txt\r\n"
    "drwxr-xr-x  658 ftp      ftp         16384 Apr 29  2025 users\r\n"
    "-rw-r--r--    1 ftp      ftp          1329 Dec 12  2007 welcome.msg\r\n";

} // namespace

// --------------------------------------------------------------------------------
// Defect 1: the hang. Anything that is not an entry -- a blank line, the end of the
// buffer -- used to come back as FUJI_ERROR::NONE with the out-params untouched,
// and open_dir() took that as "keep going" forever.
// --------------------------------------------------------------------------------

TEST_CASE("a listing always terminates")
{
    SUBCASE("trailing blank line after the last entry")
    {
        // FTP servers routinely end the data connection with a bare CRLF. This is
        // the reported repro: NDIR on ftp.untergrund.net never returned.
        auto entries = drain(unix_listing("\r\n") + "\r\n");
        CHECK(names(entries) == std::vector<std::string>{"robots.txt", "users", "welcome.msg"});
    }

    SUBCASE("several trailing blank lines")
    {
        auto entries = drain(unix_listing("\r\n") + "\r\n\r\n\r\n");
        CHECK(entries.size() == 3);
    }

    SUBCASE("blank lines between entries are skipped, not reported")
    {
        auto entries = drain(
            "-rw-r--r--    1 ftp      ftp            26 May 03  2020 robots.txt\r\n"
            "\r\n"
            "-rw-r--r--    1 ftp      ftp          1329 Dec 12  2007 welcome.msg\r\n");
        CHECK(names(entries) == std::vector<std::string>{"robots.txt", "welcome.msg"});
    }

    SUBCASE("an empty listing stops immediately and leaves the out-params alone")
    {
        std::istringstream in("");
        std::string name = "<untouched>";
        long size = -1;
        bool is_dir = true;

        CHECK(ftp_next_dir_entry(in, name, size, is_dir) == FUJI_ERROR::UNSPECIFIED);
        CHECK(name == "<untouched>");
    }

    SUBCASE("a listing of nothing but blank lines stops immediately")
    {
        std::istringstream in("\r\n\r\n\r\n");
        std::string name;
        long size = 0;
        bool is_dir = false;

        CHECK(ftp_next_dir_entry(in, name, size, is_dir) == FUJI_ERROR::UNSPECIFIED);
    }

    SUBCASE("reading past the end keeps saying stop")
    {
        std::istringstream in(unix_listing("\r\n"));
        std::string name;
        long size = 0;
        bool is_dir = false;

        // Bounded on purpose: a regression here must fail the suite, not hang it.
        size_t guard = 0;
        while (ftp_next_dir_entry(in, name, size, is_dir) == FUJI_ERROR::NONE)
            REQUIRE(++guard < 64);

        // open_dir() stops at the first non-NONE, but close_dir_handle() and a
        // re-read must not resurrect the stream.
        CHECK(ftp_next_dir_entry(in, name, size, is_dir) == FUJI_ERROR::UNSPECIFIED);
        CHECK(ftp_next_dir_entry(in, name, size, is_dir) == FUJI_ERROR::UNSPECIFIED);
    }
}

// --------------------------------------------------------------------------------
// Defect 2: the dropped last entry. The old code ended with
//   return dirBuffer.eof() ? FUJI_ERROR::UNSPECIFIED : FUJI_ERROR::NONE;
// so whenever reading the final entry also hit EOF, that entry was parsed, filled
// in, and then thrown away by the caller.
// --------------------------------------------------------------------------------

TEST_CASE("the last entry survives")
{
    SUBCASE("no trailing newline at all")
    {
        auto entries = drain(
            "-rw-r--r--    1 ftp      ftp            26 May 03  2020 robots.txt\r\n"
            "-rw-r--r--    1 ftp      ftp          1329 Dec 12  2007 welcome.msg");
        CHECK(names(entries) == std::vector<std::string>{"robots.txt", "welcome.msg"});
    }

    SUBCASE("a single entry with no trailing newline")
    {
        auto entries = drain("-rw-r--r--    1 ftp      ftp          1329 Dec 12  2007 welcome.msg");
        REQUIRE(entries.size() == 1);
        CHECK(entries[0].name == "welcome.msg");
    }

    SUBCASE("a single entry with a trailing newline")
    {
        auto entries = drain("-rw-r--r--    1 ftp      ftp          1329 Dec 12  2007 welcome.msg\r\n");
        REQUIRE(entries.size() == 1);
        CHECK(entries[0].name == "welcome.msg");
    }
}

// --------------------------------------------------------------------------------
// Defect 3: truncated names. The old code did
//   line = line.substr(0, line.size() - 1);
// unconditionally, which is right for CRLF (getline eats the \n, leaving \r) and
// wrong for everything else -- it ate a real character off every LF-terminated
// name, and underflowed on an empty line.
// --------------------------------------------------------------------------------

TEST_CASE("line endings are normalised, not assumed")
{
    SUBCASE("CRLF: the \\r is stripped and the name is whole")
    {
        auto entries = drain(unix_listing("\r\n"));
        REQUIRE(entries.size() == 3);
        for (const auto &e : entries)
            CHECK(e.name.find('\r') == std::string::npos);
        CHECK(entries[2].name == "welcome.msg");
    }

    SUBCASE("bare LF: the last character of the name is kept")
    {
        auto entries = drain(unix_listing("\n"));
        REQUIRE(entries.size() == 3);
        CHECK(names(entries) == std::vector<std::string>{"robots.txt", "users", "welcome.msg"});
    }

    SUBCASE("CRLF and LF mixed in one listing")
    {
        auto entries = drain(
            "-rw-r--r--    1 ftp      ftp            26 May 03  2020 robots.txt\n"
            "drwxr-xr-x  658 ftp      ftp         16384 Apr 29  2025 users\r\n"
            "-rw-r--r--    1 ftp      ftp          1329 Dec 12  2007 welcome.msg\n");
        CHECK(names(entries) == std::vector<std::string>{"robots.txt", "users", "welcome.msg"});
    }
}

// --------------------------------------------------------------------------------
// The new ftpparse() return check. Lines it rejects are now skipped; they used to
// be emitted as an entry literally named "???", which is what the host then saw in
// its directory listing.
// --------------------------------------------------------------------------------

TEST_CASE("unparseable lines are skipped, never surfaced")
{
    SUBCASE("the UNIX total header")
    {
        auto entries = drain(
            "total 14786\r\n"
            "-rw-r--r--    1 ftp      ftp            26 May 03  2020 robots.txt\r\n");
        CHECK(names(entries) == std::vector<std::string>{"robots.txt"});
    }

    SUBCASE("VMS banners")
    {
        auto entries = drain(
            "DISK$ANONFTP:[ANONYMOUS]\r\n"
            "Directory DISK$PCSA:[ANONYM]\r\n"
            "-rw-r--r--    1 ftp      ftp          1329 Dec 12  2007 welcome.msg\r\n"
            "Total of 11 Files, 10966 Blocks.\r\n");
        CHECK(names(entries) == std::vector<std::string>{"welcome.msg"});
    }

    SUBCASE("a listing of nothing but noise yields nothing")
    {
        auto entries = drain("total 0\r\nTotal of 0 Files, 0 Blocks.\r\n");
        CHECK(entries.empty());
    }

    SUBCASE("no entry is ever named ???")
    {
        auto entries = drain(std::string("total 14786\r\n") + kUntergrundRoot);
        for (const auto &e : entries)
            CHECK(e.name != "???");
    }
}

// --------------------------------------------------------------------------------
// What the caller does with a parsed entry: NetworkProtocolFTP::read_dir_entry()
// copies name out and hands fileSize/is_directory to the listing formatter.
// --------------------------------------------------------------------------------

TEST_CASE("entry fields")
{
    SUBCASE("a file carries its size and is not a directory")
    {
        auto entries = drain("-rw-r--r--    1 ftp      ftp          1329 Dec 12  2007 welcome.msg\r\n");
        REQUIRE(entries.size() == 1);
        CHECK(entries[0].name == "welcome.msg");
        CHECK(entries[0].size == 1329);
        CHECK(entries[0].is_dir == false);
    }

    SUBCASE("a directory is flagged")
    {
        auto entries = drain("drwxr-xr-x  658 ftp      ftp         16384 Apr 29  2025 users\r\n");
        REQUIRE(entries.size() == 1);
        CHECK(entries[0].name == "users");
        CHECK(entries[0].is_dir == true);
    }

    SUBCASE("a symlink keeps its own name, not the target")
    {
        auto entries = drain(
            "lrwxrwxrwx    1 ftp      ftp            17 Feb 03  2012 breakpoint -> users/breakpoint/\r\n");
        REQUIRE(entries.size() == 1);
        CHECK(entries[0].name == "breakpoint");
        // ftpparse cannot tell where a symlink points, so it says "try a CWD".
        CHECK(entries[0].is_dir == true);
    }

    SUBCASE("a name containing a space is not mistaken for a symlink")
    {
        auto entries = drain("-rw-r--r--    1 ftp      ftp            26 May 03  2020 read me.txt\r\n");
        REQUIRE(entries.size() == 1);
        CHECK(entries[0].name == "read me.txt");
    }
}

// --------------------------------------------------------------------------------
// The whole reported repro, end to end.
// --------------------------------------------------------------------------------

TEST_CASE("ftp.untergrund.net root, the listing from the bug report")
{
    auto entries = drain(kUntergrundRoot);

    REQUIRE(entries.size() == 4);
    CHECK(names(entries) == std::vector<std::string>{"breakpoint", "robots.txt", "users", "welcome.msg"});
    CHECK(entries[0].is_dir == true);   // symlink to a directory
    CHECK(entries[1].is_dir == false);  // robots.txt
    CHECK(entries[2].is_dir == true);   // users
    CHECK(entries[3].is_dir == false);  // welcome.msg -- last, and it must be here

    SUBCASE("and again with the trailing blank line a real server sends")
    {
        auto with_blank = drain(std::string(kUntergrundRoot) + "\r\n");
        CHECK(with_blank.size() == 4);
    }
}
