#define DOCTEST_CONFIG_IMPLEMENT_WITH_MAIN
#include <doctest/doctest.h>

#include <cstdio>
#include <cstring>
#include <regex>
#include <string>
#include <vector>

#include "pdf_printer.h"

// FileSystem links DirCache, which filters with this; these tests never list a directory.
bool util_wildcard_match(const char *, const char *) { return true; }

// Link-time stand-ins: only pdf_add_fonts() reads the font files, and no test closes a job.
long util_parseInt(FILE *f) { (void)f; return 0; }
long util_parseInt(FILE *f, char skipChar) { (void)f; (void)skipChar; return 0; }

namespace
{

// The Epson 80 page: letter size, 12 pt lines from y=776, 80 columns of 7.2 pt.
class testPrinter : public pdfPrinter
{
public:
    testPrinter()
    {
        _file = tmpfile();
        _eol = ASCII_CR;
        pageWidth = 612.0;
        pageHeight = 792.0;
        topMargin = 16.0;
        bottomMargin = 0.0;
        leftMargin = 18.0;
        printWidth = 576.0;
        lineHeight = 12.0;
        charWidth = 7.2;
        fontNumber = 1;
        fontSize = 9;
    }

    // One WRITE: the buffer that a bus device passes to process().
    void write(const std::string &s)
    {
        REQUIRE(s.size() <= 255);
        memcpy(buffer, s.data(), s.size());
        process_buffer(s.size(), 0, 0);
    }

    void write_in_chunks(const std::string &s, size_t len)
    {
        for (size_t i = 0; i < s.size(); i += len)
            write(s.substr(i, len));
    }

    bool page_open() { return !TOPflag; }

    std::string stream()
    {
        fflush(_file);
        long end = ftell(_file);
        std::string out(end, '\0');
        rewind(_file);
        REQUIRE(fread(out.data(), 1, end, _file) == (size_t)end);
        fseek(_file, end, SEEK_SET);
        return out;
    }

    const char *modelname() override { return "test"; }

protected:
    void post_new_file() override {}
    void pdf_clear_modes() override {}
    void pdf_handle_char(uint16_t c, uint8_t aux1, uint8_t aux2) override
    {
        (void)aux1;
        (void)aux2;
        if (c >= ' ')
        {
            fputc(c, _file);
            pdf_X += charWidth;
        }
    }
};

struct Line
{
    double y;
    std::string text;
};

// The lines of each page, at the y position the PDF draws them.
std::vector<std::vector<Line>> pages(const std::string &stream)
{
    std::vector<std::vector<Line>> out;
    std::regex token(R"(BT\n|(-?[0-9.]+) (-?[0-9.]+) Td|\[\(([^)]*)\)\]TJ)");
    double y = 0;
    bool page_start = false;
    for (std::sregex_iterator t(stream.begin(), stream.end(), token), end; t != end; ++t)
    {
        if ((*t)[0] == "BT\n")
        {
            out.emplace_back();
            page_start = true;
        }
        else if ((*t)[1].matched)
        {
            // the first Td of a page is absolute, the others move from the line before
            y = page_start ? std::stod((*t)[2]) : y + std::stod((*t)[2]);
            page_start = false;
        }
        else if (!out.empty() && (*t)[3].length() > 0)
            out.back().push_back({y, (*t)[3]});
    }
    return out;
}

std::string numbered_lines(int count, const char *eol)
{
    std::string s;
    char line[16];
    for (int i = 1; i <= count; i++)
    {
        snprintf(line, sizeof(line), "L%02d%s", i, eol);
        s += line;
    }
    return s;
}

} // namespace

TEST_CASE("a buffer that holds many lines ends the page at the bottom margin")
{
    // 100-byte buffers: the page fills in the middle of the third one
    testPrinter p;
    p.write_in_chunks(numbered_lines(70, "\r"), 100);

    auto pg = pages(p.stream());
    REQUIRE(pg.size() == 2);
    REQUIRE(pg[0].size() == 64);
    CHECK(pg[0].front().text == "L01");
    CHECK(pg[0].front().y == 764);
    CHECK(pg[0].back().text == "L64");
    CHECK(pg[0].back().y == 8);
    CHECK(pg[1].front().text == "L65");
    CHECK(pg[1].front().y == 764);
}

TEST_CASE("the pages do not depend on how the text is split into buffers")
{
    std::string text = numbered_lines(70, "\r") + std::string(200, 'x') + "\r";

    testPrinter by_line;
    for (int i = 1; i <= 70; i++)
        by_line.write(numbered_lines(i, "\r").substr(4 * (i - 1)));
    by_line.write(std::string(200, 'x') + "\r");
    std::string expected = by_line.stream();

    for (size_t len : {1, 7, 80, 128, 255})
    {
        CAPTURE(len);
        testPrinter p;
        p.write_in_chunks(text, len);
        CHECK(p.stream() == expected);
    }
}

TEST_CASE("an automatic CR at the bottom of a page starts the next page")
{
    testPrinter p;
    p.write_in_chunks(numbered_lines(63, "\r"), 128);
    p.write(std::string(80, 'a') + std::string(40, 'b') + "\r");

    auto pg = pages(p.stream());
    REQUIRE(pg.size() == 2);
    CHECK(pg[0].back().text == std::string(80, 'a'));
    CHECK(pg[0].back().y == 8);
    REQUIRE(pg[1].size() == 1);
    CHECK(pg[1].front().text == std::string(40, 'b'));
    CHECK(pg[1].front().y == 764);
    for (auto &page : pg)
        for (auto &line : page)
            CHECK(line.y >= 0);
}

TEST_CASE("text that exactly fills a page leaves no page open")
{
    testPrinter p;
    p.write_in_chunks(numbered_lines(64, "\r"), 128);
    CHECK_FALSE(p.page_open());
    CHECK(pages(p.stream()).size() == 1);
}

#ifdef BUILD_RS232
TEST_CASE("RS232: the LF after the CR that fills a page does not open a page")
{
    testPrinter p;
    p.write_in_chunks(numbered_lines(64, "\r\n"), 128);
    CHECK_FALSE(p.page_open());
    CHECK(pages(p.stream()).size() == 1);
}
#endif
