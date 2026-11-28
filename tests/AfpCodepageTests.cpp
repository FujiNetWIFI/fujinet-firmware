#define DOCTEST_CONFIG_IMPLEMENT_WITH_MAIN
#include <doctest/doctest.h>

extern "C" {
#include "afp_protocol.h"
#include "codepage.h"
}

#include <string>

static std::string to_afp(char encoding, const char *path, int *result = nullptr)
{
    char out[AFP_MAX_PATH];
    int r = convert_path_to_afp(encoding, out, const_cast<char *>(path), sizeof(out));
    if (result) *result = r;
    return r == 0 ? std::string(out) : std::string("<error>");
}

static std::string to_unix(char encoding, const char *name)
{
    char out[AFP_MAX_PATH];
    if (convert_path_to_unix(encoding, out, const_cast<char *>(name), sizeof(out)) != 0)
        return "<error>";
    return out;
}

TEST_CASE("a long-name volume gets MacRoman paths")
{
    CHECK(to_afp(kFPLongName, "/Caf\xc3\xa9/Read Me") == "/Caf\x8e/Read Me");
    CHECK(to_afp(kFPLongName, "/plain/ascii.txt") == "/plain/ascii.txt");
    CHECK(to_afp(kFPLongName, "/") == "/");
}

TEST_CASE("a long-name volume's names come back as UTF-8")
{
    CHECK(to_unix(kFPLongName, "Caf\x8e") == "Caf\xc3\xa9");
    CHECK(to_unix(kFPLongName, "Andr\x8e\xd5s Mac") == "Andr\xc3\xa9\xe2\x80\x99s Mac");
    CHECK(to_unix(kFPLongName, "x") == "x");
}

TEST_CASE("a path MacRoman cannot hold is refused")
{
    int result = 0;
    CHECK(to_afp(kFPLongName, "/\xe6\x97\xa5.txt", &result) == "<error>");
    CHECK(result == -1);
}

TEST_CASE("a UTF-8 volume is unchanged: decomposed on the wire, precomposed here")
{
    CHECK(to_afp(kFPUTF8Name, "/\xc3\xa4.txt") == "/a\xcc\x88.txt");
    CHECK(to_unix(kFPUTF8Name, "a\xcc\x88.txt") == "\xc3\xa4.txt");
}
