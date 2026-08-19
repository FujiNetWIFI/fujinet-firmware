#define DOCTEST_CONFIG_IMPLEMENT_WITH_MAIN
#include <doctest/doctest.h>

#include "fntext/fn_sanitize.h"

#include <string>

TEST_CASE("fn_sanitize text transformation functions")
{
    // ========================================================================
    // fn_decode_entities: Decode HTML/XML character entities into UTF-8
    // ========================================================================

    SUBCASE("fn_decode_entities: XML predefined entities")
    {
        CHECK(fn_decode_entities("&amp;") == "&");
        CHECK(fn_decode_entities("&lt;") == "<");
        CHECK(fn_decode_entities("&gt;") == ">");
        CHECK(fn_decode_entities("&quot;") == "\"");
        CHECK(fn_decode_entities("&apos;") == "'");
        CHECK(fn_decode_entities("&lt;&gt;&amp;") == "<>&");
    }

    SUBCASE("fn_decode_entities: Common named entities decode to exact UTF-8")
    {
        // Non-breaking space (U+00A0)
        CHECK(fn_decode_entities("&nbsp;") == "\xC2\xA0");

        // Em dash (U+2014)
        CHECK(fn_decode_entities("&mdash;") == "\xE2\x80\x94");

        // En dash (U+2013)
        CHECK(fn_decode_entities("&ndash;") == "\xE2\x80\x93");

        // Ellipsis (U+2026)
        CHECK(fn_decode_entities("&hellip;") == "\xE2\x80\xA6");

        // Left single quotation mark (U+2018)
        CHECK(fn_decode_entities("&lsquo;") == "\xE2\x80\x98");

        // Right single quotation mark (U+2019)
        CHECK(fn_decode_entities("&rsquo;") == "\xE2\x80\x99");

        // Left double quotation mark (U+201C)
        CHECK(fn_decode_entities("&ldquo;") == "\xE2\x80\x9C");

        // Right double quotation mark (U+201D)
        CHECK(fn_decode_entities("&rdquo;") == "\xE2\x80\x9D");

        // Latin small letter e with acute (U+00E9)
        CHECK(fn_decode_entities("&eacute;") == "\xC3\xA9");

        // Latin small letter n with tilde (U+00F1)
        CHECK(fn_decode_entities("&ntilde;") == "\xC3\xB1");

        // Copyright sign (U+00A9)
        CHECK(fn_decode_entities("&copy;") == "\xC2\xA9");

        // Trademark sign (U+2122)
        CHECK(fn_decode_entities("&trade;") == "\xE2\x84\xA2");

        // Euro sign (U+20AC)
        CHECK(fn_decode_entities("&euro;") == "\xE2\x82\xAC");
    }

    SUBCASE("fn_decode_entities: Case sensitivity of entity names")
    {
        // &dagger; and &Dagger; are different entities
        CHECK(fn_decode_entities("&dagger;") == "\xE2\x80\xA0");  // (U+2020)
        CHECK(fn_decode_entities("&Dagger;") == "\xE2\x80\xA1");  // (U+2021)

        // &AMP; (wrong case) is not recognized and passes through
        CHECK(fn_decode_entities("&AMP;") == "&AMP;");

        // &Amp; (wrong case) passes through
        CHECK(fn_decode_entities("&Amp;") == "&Amp;");
    }

    SUBCASE("fn_decode_entities: Numeric decimal entities")
    {
        // &#8212; = em dash (U+2014)
        CHECK(fn_decode_entities("&#8212;") == "\xE2\x80\x94");

        // &#65; = 'A'
        CHECK(fn_decode_entities("&#65;") == "A");

        // &#32; = space
        CHECK(fn_decode_entities("&#32;") == " ");
    }

    SUBCASE("fn_decode_entities: Numeric hexadecimal entities")
    {
        // &#x2014; = em dash (U+2014)
        CHECK(fn_decode_entities("&#x2014;") == "\xE2\x80\x94");

        // &#X2014; (uppercase X) = em dash
        CHECK(fn_decode_entities("&#X2014;") == "\xE2\x80\x94");

        // &#x41; = 'A'
        CHECK(fn_decode_entities("&#x41;") == "A");

        // &#X41; (uppercase X)
        CHECK(fn_decode_entities("&#X41;") == "A");
    }

    SUBCASE("fn_decode_entities: No double-decoding")
    {
        // "&amp;lt;" must come out as "&lt;" (the literal string), not "<"
        CHECK(fn_decode_entities("&amp;lt;") == "&lt;");

        // Verify the &lt; in the result is not decoded a second time
        CHECK(fn_decode_entities("&amp;lt;") != "<");

        // More complex: &amp; decodes to &, but the & in it is not re-processed
        CHECK(fn_decode_entities("&amp;amp;") == "&amp;");
    }

    SUBCASE("fn_decode_entities: Unrecognized entities pass through verbatim")
    {
        // Bare ampersand without a semicolon
        CHECK(fn_decode_entities("&") == "&");

        // Unrecognized entity name
        CHECK(fn_decode_entities("&foo;") == "&foo;");

        // Empty numeric entity
        CHECK(fn_decode_entities("&#;") == "&#;");

        // Invalid hex entity
        CHECK(fn_decode_entities("&#xZZ;") == "&#xZZ;");

        // Unterminated entity (no semicolon)
        CHECK(fn_decode_entities("&amp") == "&amp");

        // Out-of-range numeric value
        CHECK(fn_decode_entities("&#99999999;") == "&#99999999;");

        // Null code point (U+0000)
        CHECK(fn_decode_entities("&#0;") == "&#0;");

        // Surrogate half (U+D800, not a valid Unicode scalar)
        CHECK(fn_decode_entities("&#xD800;") == "&#xD800;");
    }

    SUBCASE("fn_decode_entities: Entity names that were once missing or misspelled")
    {
        // Regression guard: a name absent from (or misspelled in) the table
        // silently passes through, leaking literal "&emsp;" into output that
        // is supposed to be plain ASCII. Each of these was a real defect.
        CHECK(fn_decode_entities("&ordf;") == "\xC2\xAA");   // (U+00AA) was typo'd "orfa"
        CHECK(fn_decode_entities("&ensp;") == "\xE2\x80\x82"); // (U+2002)
        CHECK(fn_decode_entities("&emsp;") == "\xE2\x80\x83"); // (U+2003)
        CHECK(fn_decode_entities("&Omega;") == "\xCE\xA9");  // (U+03A9)
        CHECK(fn_decode_entities("&loz;") == "\xE2\x97\x8A"); // (U+25CA)

        // Nothing an 8-bit host receives should still contain an entity.
        CHECK(fn_sanitize_ascii("a&emsp;b&ordf;c").find('&') == std::string::npos);
    }

    SUBCASE("fn_decode_entities: Empty string and strings with no entities")
    {
        CHECK(fn_decode_entities("") == "");
        CHECK(fn_decode_entities("hello world") == "hello world");
        CHECK(fn_decode_entities("no entities here!") == "no entities here!");
        CHECK(fn_decode_entities("123 &!#%") == "123 &!#%");
    }

    SUBCASE("fn_decode_entities: Mixed entities in one string")
    {
        CHECK(fn_decode_entities("&lt;tag&gt;") == "<tag>");
        CHECK(fn_decode_entities("Price: &euro;100") == "Price: \xE2\x82\xAC" "100");
        CHECK(fn_decode_entities("&copy;2024&mdash;now") == "\xC2\xA9" "2024\xE2\x80\x94" "now");
    }

    // ========================================================================
    // fn_utf8_to_ascii: Fold UTF-8 text to printable ASCII (0x20-0x7E)
    // ========================================================================

    SUBCASE("fn_utf8_to_ascii: ASCII 0x20-0x7E passes through untouched")
    {
        CHECK(fn_utf8_to_ascii("") == "");
        CHECK(fn_utf8_to_ascii("hello") == "hello");
        CHECK(fn_utf8_to_ascii("HELLO") == "HELLO");
        CHECK(fn_utf8_to_ascii("123") == "123");
        CHECK(fn_utf8_to_ascii("!@#$%^&*()") == "!@#$%^&*()");
        // Every printable byte 0x20-0x7E survives unchanged; DEL (0x7F) does not.
        std::string printable;
        for (int c = 0x20; c <= 0x7E; c++)
            printable += (char)c;
        CHECK(fn_utf8_to_ascii(printable) == printable);
        CHECK(fn_utf8_to_ascii("a\x7F" "b") == "ab");
    }

    SUBCASE("fn_utf8_to_ascii: Whitespace normalization")
    {
        // Tab (0x09) becomes space
        CHECK(fn_utf8_to_ascii("hello\tworld") == "hello world");

        // Line feed (0x0A) becomes space
        CHECK(fn_utf8_to_ascii("line1\nline2") == "line1 line2");

        // Carriage return (0x0D) becomes space
        CHECK(fn_utf8_to_ascii("line1\rline2") == "line1 line2");

        // Combined
        CHECK(fn_utf8_to_ascii("a\t\n\rb") == "a   b");
    }

    SUBCASE("fn_utf8_to_ascii: UTF-8 punctuation and symbols transliterate")
    {
        // Em dash (U+2014) -> "--"
        CHECK(fn_utf8_to_ascii("\xE2\x80\x94") == "--");

        // En dash (U+2013) -> "-"
        CHECK(fn_utf8_to_ascii("\xE2\x80\x93") == "-");

        // Left single quotation mark (U+2018) -> "'"
        CHECK(fn_utf8_to_ascii("\xE2\x80\x98") == "'");

        // Right single quotation mark (U+2019) -> "'"
        CHECK(fn_utf8_to_ascii("\xE2\x80\x99") == "'");

        // Left double quotation mark (U+201C) -> "\""
        CHECK(fn_utf8_to_ascii("\xE2\x80\x9C") == "\"");

        // Right double quotation mark (U+201D) -> "\""
        CHECK(fn_utf8_to_ascii("\xE2\x80\x9D") == "\"");

        // Ellipsis (U+2026) -> "..."
        CHECK(fn_utf8_to_ascii("\xE2\x80\xA6") == "...");

        // Non-breaking space (U+00A0) -> " "
        CHECK(fn_utf8_to_ascii("\xC2\xA0") == " ");

        // Bullet (U+2022) -> "*"
        CHECK(fn_utf8_to_ascii("\xE2\x80\xA2") == "*");
    }

    SUBCASE("fn_utf8_to_ascii: Symbols for copyright, registered, trademark")
    {
        // Copyright (U+00A9) -> "(c)"
        CHECK(fn_utf8_to_ascii("\xC2\xA9") == "(c)");

        // Registered trademark (U+00AE) -> "(R)"
        CHECK(fn_utf8_to_ascii("\xC2\xAE") == "(R)");

        // Trademark (U+2122) -> "(tm)"
        CHECK(fn_utf8_to_ascii("\xE2\x84\xA2") == "(tm)");
    }

    SUBCASE("fn_utf8_to_ascii: Accented Latin letters fold to base letters")
    {
        // café -> cafe
        CHECK(fn_utf8_to_ascii("caf\xC3\xA9") == "cafe");

        // naïve -> naive
        CHECK(fn_utf8_to_ascii("na\xC3\xAF" "ve") == "naive");

        // Zürich -> Zurich
        CHECK(fn_utf8_to_ascii("Z\xC3\xBCrich") == "Zurich");

        // German ß (U+00DF) -> "ss"
        CHECK(fn_utf8_to_ascii("\xC3\x9F") == "ss");

        // æ (U+00E6) -> "ae"
        CHECK(fn_utf8_to_ascii("\xC3\xA6") == "ae");

        // Ø (U+00D8) -> "O"
        CHECK(fn_utf8_to_ascii("\xC3\x98") == "O");

        // Comprehensive accented letter test
        CHECK(fn_utf8_to_ascii("\xC3\xA0\xC3\xA1\xC3\xA2\xC3\xA3\xC3\xA4\xC3\xA5") == "aaaaaa");
    }

    SUBCASE("fn_utf8_to_ascii: Characters with no ASCII equivalent are dropped")
    {
        // CJK character (U+4E2D, Chinese "zhong") disappears
        // "a" + CJK + "b" -> "ab"
        CHECK(fn_utf8_to_ascii("a\xE4\xB8\xAD" "b") == "ab");

        // Emoji (U+1F600, grinning face) is 4-byte UTF-8 and disappears
        // "a" + emoji + "b" -> "ab"
        CHECK(fn_utf8_to_ascii("a\xF0\x9F\x98\x80" "b") == "ab");

        // Multiple unmappable characters
        CHECK(fn_utf8_to_ascii("a\xE4\xB8\xAD" "a\xE4\xB8\xAD" "b") == "aab");
    }

    SUBCASE("fn_utf8_to_ascii: Malformed UTF-8 is dropped, ASCII survives")
    {
        // Lone continuation byte (0x80) is dropped
        CHECK(fn_utf8_to_ascii("a\x80" "b") == "ab");

        // Truncated 2-byte sequence at end (0xC3 is start of 2-byte, but missing second)
        CHECK(fn_utf8_to_ascii("a\xC3") == "a");

        // Truncated 3-byte sequence at end (0xE2 0x80 is incomplete)
        CHECK(fn_utf8_to_ascii("a\xE2\x80") == "a");

        // Overlong encoding (0xC0 0x80 encodes U+0000 in 2 bytes, not valid)
        CHECK(fn_utf8_to_ascii("a\xC0\x80" "b") == "ab");

        // Stray 0xFF (invalid start byte)
        CHECK(fn_utf8_to_ascii("a\xFF" "b") == "ab");

        // NUL byte embedded mid-string (built with explicit length)
        CHECK(fn_utf8_to_ascii(std::string("a\0b", 3)) == "ab");
    }

    SUBCASE("fn_utf8_to_ascii: Empty string")
    {
        CHECK(fn_utf8_to_ascii("") == "");
    }

    // ========================================================================
    // fn_sanitize_ascii: Combine entity decoding and UTF-8 to ASCII conversion
    // ========================================================================

    SUBCASE("fn_sanitize_ascii: Equals fn_utf8_to_ascii(fn_decode_entities(x))")
    {
        auto test_sanitize = [](const std::string &input) {
            std::string expected = fn_utf8_to_ascii(fn_decode_entities(input));
            CHECK(fn_sanitize_ascii(input) == expected);
        };

        test_sanitize("");
        test_sanitize("hello world");
        test_sanitize("&amp;");
        test_sanitize("&mdash;");
        test_sanitize("&#8212;");
        test_sanitize("test &amp; more &copy;");
        test_sanitize("caf&eacute;");
    }

    SUBCASE("fn_sanitize_ascii: End-to-end RSS-style title")
    {
        // "AT&amp;T &mdash; &ldquo;caf&eacute;&rdquo;&hellip;"
        // Decode entities: "AT&T -- \"caf\xC3\xA9\""...
        // UTF-8 to ASCII: "AT&T -- \"cafe\"..."
        CHECK(fn_sanitize_ascii("AT&amp;T &mdash; &ldquo;caf&eacute;&rdquo;&hellip;") ==
              "AT&T -- \"cafe\"...");
    }

    SUBCASE("fn_sanitize_ascii: Mixed entities, numeric refs, raw UTF-8, newline")
    {
        // Named entity: &copy;
        // Numeric ref: &#8212; (em dash)
        // Raw UTF-8: ß (U+00DF)
        // Newline: \n
        // Expected: "(c)" + "--" + "ss" + " " = "(c)--ss "
        std::string input = "&copy;&#8212;" "\xC3\x9F" "\n";
        CHECK(fn_sanitize_ascii(input) == "(c)--ss ");
    }

    SUBCASE("fn_sanitize_ascii: Complex real-world example")
    {
        // A complex string with multiple entity types and UTF-8
        std::string input =
            "Breaking News"
            "\n"  // newline becomes space
            "&mdash;"  // em dash becomes --
            " "
            "&ldquo;"  // left quote becomes "
            "New features"
            "&rdquo;"  // right quote becomes "
            " &euro;9.99"  // euro and number, euro becomes unchanged or transliterated
            " "
            "\xE2\x80\xA6";  // ellipsis becomes ...

        std::string output = fn_sanitize_ascii(input);

        // Check that it contains expected parts
        CHECK(output.find("Breaking News") != std::string::npos);
        CHECK(output.find("--") != std::string::npos);
        CHECK(output.find("\"") != std::string::npos);
        CHECK(output.find("...") != std::string::npos);
        // Euro should be gone (no ASCII equivalent)
        CHECK(output.find("\xE2\x82\xAC") == std::string::npos);
    }
}
