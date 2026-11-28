#define DOCTEST_CONFIG_IMPLEMENT_WITH_MAIN
#include <doctest/doctest.h>

#include "macroman.h"

#include <cstring>
#include <string>

static std::string convert(const char *src)
{
    char out[64];
    REQUIRE(afp_macroman_to_utf8(src, out, sizeof(out)) == 0);
    return out;
}

TEST_CASE("ASCII passes through unchanged")
{
    CHECK(convert("Shared Files 01") == "Shared Files 01");
    CHECK(convert("") == "");
}

TEST_CASE("two-byte characters")
{
    CHECK(convert("Caf\x8e") == "Caf\xc3\xa9");        // e acute
    CHECK(convert("\x80") == "\xc3\x84");              // A diaeresis, first of the table
    CHECK(convert("\xca") == "\xc2\xa0");              // no-break space
    CHECK(convert("\xff") == "\xcb\x87");              // caron, last of the table
}

TEST_CASE("three-byte characters")
{
    CHECK(convert("\xa5") == "\xe2\x80\xa2");          // bullet
    CHECK(convert("\xdb") == "\xe2\x82\xac");          // euro sign
    CHECK(convert("\xf0") == "\xef\xa3\xbf");          // Apple logo, private use
    CHECK(convert("\xaa") == "\xe2\x84\xa2");          // trade mark
}

TEST_CASE("a mixed name")
{
    CHECK(convert("Andr\x8e\xd5s Mac") == "Andr\xc3\xa9\xe2\x80\x99s Mac");
}

TEST_CASE("a destination that is too small stops on a character boundary")
{
    char out[5];
    std::memset(out, 'x', sizeof(out));
    // "ab" + bullet needs 2 + 3 + NUL = 6 bytes.
    CHECK(afp_macroman_to_utf8("ab\xa5", out, sizeof(out)) == -1);
    CHECK(std::string(out) == "ab");
}

TEST_CASE("an exact fit succeeds")
{
    char out[6];
    CHECK(afp_macroman_to_utf8("ab\xa5", out, sizeof(out)) == 0);
    CHECK(std::string(out) == "ab\xe2\x80\xa2");
}

TEST_CASE("a zero-length destination is reported and left untouched")
{
    char out[1] = {'x'};
    CHECK(afp_macroman_to_utf8("a", out, 0) == -1);
    CHECK(out[0] == 'x');
}

static std::string back(const char *src)
{
    char out[64];
    REQUIRE(afp_utf8_to_macroman(src, out, sizeof(out)) == 0);
    return out;
}

TEST_CASE("UTF-8 back to MacRoman")
{
    CHECK(back("Shared Files 01") == "Shared Files 01");
    CHECK(back("") == "");
    CHECK(back("Caf\xc3\xa9") == "Caf\x8e");
    CHECK(back("\xe2\x80\xa2") == "\xa5");             // bullet
    CHECK(back("\xe2\x82\xac") == "\xdb");             // euro sign
    CHECK(back("\xef\xa3\xbf") == "\xf0");             // Apple logo
    CHECK(back("Andr\xc3\xa9\xe2\x80\x99s Mac") == "Andr\x8e\xd5s Mac");
}

TEST_CASE("every MacRoman byte survives the round trip")
{
    for (int c = 1; c < 256; c++)
    {
        const char in[2] = {static_cast<char>(c), 0};
        char utf8[8], out[4];
        REQUIRE(afp_macroman_to_utf8(in, utf8, sizeof(utf8)) == 0);
        REQUIRE(afp_utf8_to_macroman(utf8, out, sizeof(out)) == 0);
        CHECK(static_cast<unsigned char>(out[0]) == c);
        CHECK(out[1] == 0);
    }
}

TEST_CASE("what MacRoman cannot hold is refused, not guessed")
{
    char out[16];
    CHECK(afp_utf8_to_macroman("\xe6\x97\xa5", out, sizeof(out)) == -1);   // CJK
    CHECK(afp_utf8_to_macroman("\xf0\x9f\x98\x80", out, sizeof(out)) == -1); // emoji
    CHECK(afp_utf8_to_macroman("e\xcc\x81", out, sizeof(out)) == -1);      // combining accent
    CHECK(afp_utf8_to_macroman("\xc3", out, sizeof(out)) == -1);           // cut short
    CHECK(afp_utf8_to_macroman("\x8e", out, sizeof(out)) == -1);           // not UTF-8
    CHECK(afp_utf8_to_macroman("\xc0\xae", out, sizeof(out)) == -1);       // overlong '.'
}

TEST_CASE("a MacRoman destination that is too small is refused")
{
    char out[3];
    CHECK(afp_utf8_to_macroman("abc", out, sizeof(out)) == -1);
    CHECK(afp_utf8_to_macroman("ab", out, sizeof(out)) == 0);
    CHECK(std::string(out) == "ab");
    CHECK(afp_utf8_to_macroman("x", out, 0) == -1);
}
