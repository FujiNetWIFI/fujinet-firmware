/**
 * Text sanitization helpers for #FujiNet query results.
 *
 * Decodes HTML/XML entities and folds UTF-8 to printable ASCII, with
 * transliteration for common special characters.
 */

#include "fn_sanitize.h"

#include <cstdint>
#include <cstring>
#include <cstdlib>
#include <cctype>

namespace {

// ============================================================================
// Named Entity Table
// ============================================================================

struct NamedEntity {
    const char *name;
    const char *utf8;
};

// Sorted alphabetically. Searches via linear scan (table is ~100 entries).
// UTF-8 sequences verified against Unicode code points.
static const NamedEntity kNamedEntities[] = {
    {"AElig", "\xc3\x86"},      // 0x00C6
    {"Aacute", "\xc3\x81"},     // 0x00C1
    {"Acirc", "\xc3\x82"},      // 0x00C2
    {"Agrave", "\xc3\x80"},     // 0x00C0
    {"Aring", "\xc3\x85"},      // 0x00C5
    {"Atilde", "\xc3\x83"},     // 0x00C3
    {"Auml", "\xc3\x84"},       // 0x00C4
    {"Ccedil", "\xc3\x87"},     // 0x00C7
    {"Dagger", "\xe2\x80\xa1"}, // 0x2021
    {"Delta", "\xce\x94"},      // 0x0394
    {"ETH", "\xc3\x90"},        // 0x00D0
    {"Eacute", "\xc3\x89"},     // 0x00C9
    {"Ecirc", "\xc3\x8a"},      // 0x00CA
    {"Egrave", "\xc3\x88"},     // 0x00C8
    {"Euml", "\xc3\x8b"},       // 0x00CB
    {"Iacute", "\xc3\x8d"},     // 0x00CD
    {"Icirc", "\xc3\x8e"},      // 0x00CE
    {"Igrave", "\xc3\x8c"},     // 0x00CC
    {"Iuml", "\xc3\x8f"},       // 0x00CF
    {"Ntilde", "\xc3\x91"},     // 0x00D1
    {"Oacute", "\xc3\x93"},     // 0x00D3
    {"Ocirc", "\xc3\x94"},      // 0x00D4
    {"Ograve", "\xc3\x92"},     // 0x00D2
    {"Oslash", "\xc3\x98"},     // 0x00D8
    {"Omega", "\xce\xa9"},        // 0x03A9
    {"Otilde", "\xc3\x95"},     // 0x00D5
    {"Ouml", "\xc3\x96"},       // 0x00D6
    {"Prime", "\xe2\x80\xb3"},  // 0x2033
    {"Sigma", "\xce\xa3"},      // 0x03A3
    {"THORN", "\xc3\x9e"},      // 0x00DE
    {"Uacute", "\xc3\x9a"},     // 0x00DA
    {"Ucirc", "\xc3\x9b"},      // 0x00DB
    {"Ugrave", "\xc3\x99"},     // 0x00D9
    {"Uuml", "\xc3\x9c"},       // 0x00DC
    {"Yacute", "\xc3\x9d"},     // 0x00DD
    {"aacute", "\xc3\xa1"},     // 0x00E1
    {"acirc", "\xc3\xa2"},      // 0x00E2
    {"acute", "\xc2\xb4"},      // 0x00B4
    {"aelig", "\xc3\xa6"},      // 0x00E6
    {"agrave", "\xc3\xa0"},     // 0x00E0
    {"alpha", "\xce\xb1"},      // 0x03B1
    {"amp", "&"},               // 0x0026
    {"and", "\xe2\x88\xa7"},    // 0x2227
    {"apos", "'"},              // 0x0027
    {"aring", "\xc3\xa5"},      // 0x00E5
    {"asymp", "\xe2\x89\x88"},  // 0x2248
    {"atilde", "\xc3\xa3"},     // 0x00E3
    {"auml", "\xc3\xa4"},       // 0x00E4
    {"beta", "\xce\xb2"},       // 0x03B2
    {"bdquo", "\xe2\x80\x9e"},  // 0x201E
    {"brvbar", "\xc2\xa6"},     // 0x00A6
    {"bull", "\xe2\x80\xa2"},   // 0x2022
    {"ccedil", "\xc3\xa7"},     // 0x00E7
    {"cedil", "\xc2\xb8"},      // 0x00B8
    {"cent", "\xc2\xa2"},       // 0x00A2
    {"clubs", "\xe2\x99\xa3"},  // 0x2663
    {"copy", "\xc2\xa9"},       // 0x00A9
    {"curren", "\xc2\xa4"},     // 0x00A4
    {"dagger", "\xe2\x80\xa0"}, // 0x2020
    {"darr", "\xe2\x86\x93"},   // 0x2193
    {"deg", "\xc2\xb0"},        // 0x00B0
    {"delta", "\xce\xb4"},      // 0x03B4
    {"diams", "\xe2\x99\xa6"},  // 0x2666
    {"divide", "\xc3\xb7"},     // 0x00F7
    {"egrave", "\xc3\xa8"},     // 0x00E8
    {"eacute", "\xc3\xa9"},     // 0x00E9
    {"ecirc", "\xc3\xaa"},      // 0x00EA
    {"euml", "\xc3\xab"},       // 0x00EB
    {"emsp", "\xe2\x80\x83"},    // 0x2003
    {"ensp", "\xe2\x80\x82"},    // 0x2002
    {"eth", "\xc3\xb0"},        // 0x00F0
    {"euro", "\xe2\x82\xac"},   // 0x20AC
    {"frac12", "\xc2\xbd"},     // 0x00BD
    {"frac14", "\xc2\xbc"},     // 0x00BC
    {"frac34", "\xc2\xbe"},     // 0x00BE
    {"gamma", "\xce\xb3"},      // 0x03B3
    {"ge", "\xe2\x89\xa5"},     // 0x2265
    {"gt", ">"},                // 0x003E
    {"harr", "\xe2\x86\x94"},   // 0x2194
    {"hearts", "\xe2\x99\xa5"}, // 0x2665
    {"hellip", "\xe2\x80\xa6"}, // 0x2026
    {"iacute", "\xc3\xad"},     // 0x00ED
    {"icirc", "\xc3\xae"},      // 0x00EE
    {"iexcl", "\xc2\xa1"},      // 0x00A1
    {"igrave", "\xc3\xac"},     // 0x00EC
    {"infin", "\xe2\x88\x9e"},  // 0x221E
    {"iuml", "\xc3\xaf"},       // 0x00EF
    {"iquest", "\xc2\xbf"},     // 0x00BF
    {"lambda", "\xce\xbb"},     // 0x03BB
    {"laquo", "\xc2\xab"},      // 0x00AB
    {"larr", "\xe2\x86\x90"},   // 0x2190
    {"loz", "\xe2\x97\x8a"},     // 0x25CA
    {"lsaquo", "\xe2\x80\xb9"}, // 0x2039
    {"lsquo", "\xe2\x80\x98"},  // 0x2018
    {"lt", "<"},                // 0x003C
    {"ldquo", "\xe2\x80\x9c"},  // 0x201C
    {"le", "\xe2\x89\xa4"},     // 0x2264
    {"macr", "\xc2\xaf"},       // 0x00AF
    {"mdash", "\xe2\x80\x94"},  // 0x2014
    {"micro", "\xc2\xb5"},      // 0x00B5
    {"middot", "\xc2\xb7"},     // 0x00B7
    {"minus", "\xe2\x88\x92"},  // 0x2212
    {"mu", "\xce\xbc"},         // 0x03BC
    {"nbsp", "\xc2\xa0"},       // 0x00A0
    {"ndash", "\xe2\x80\x93"},  // 0x2013
    {"ne", "\xe2\x89\xa0"},     // 0x2260
    {"not", "\xc2\xac"},        // 0x00AC
    {"ntilde", "\xc3\xb1"},     // 0x00F1
    {"oacute", "\xc3\xb3"},     // 0x00F3
    {"ocirc", "\xc3\xb4"},      // 0x00F4
    {"ograve", "\xc3\xb2"},     // 0x00F2
    {"ordm", "\xc2\xba"},       // 0x00BA
    {"ordf", "\xc2\xaa"},       // 0x00AA
    {"oslash", "\xc3\xb8"},     // 0x00F8
    {"otilde", "\xc3\xb5"},     // 0x00F5
    {"ouml", "\xc3\xb6"},       // 0x00F6
    {"para", "\xc2\xb6"},       // 0x00B6
    {"permil", "\xe2\x80\xb0"}, // 0x2030
    {"pi", "\xcf\x80"},         // 0x03C0
    {"plusmn", "\xc2\xb1"},     // 0x00B1
    {"pound", "\xc2\xa3"},      // 0x00A3
    {"prime", "\xe2\x80\xb2"},  // 0x2032
    {"quot", "\""},             // 0x0022
    {"rarr", "\xe2\x86\x92"},   // 0x2192
    {"rdquo", "\xe2\x80\x9d"},  // 0x201D
    {"reg", "\xc2\xae"},        // 0x00AE
    {"raquo", "\xc2\xbb"},      // 0x00BB
    {"rsaquo", "\xe2\x80\xba"}, // 0x203A
    {"rsquo", "\xe2\x80\x99"},  // 0x2019
    {"sbquo", "\xe2\x80\x9a"},  // 0x201A
    {"sect", "\xc2\xa7"},       // 0x00A7
    {"shy", "\xc2\xad"},        // 0x00AD
    {"sigma", "\xcf\x83"},      // 0x03C3
    {"spades", "\xe2\x99\xa0"}, // 0x2660
    {"sup1", "\xc2\xb9"},       // 0x00B9
    {"sup2", "\xc2\xb2"},       // 0x00B2
    {"sup3", "\xc2\xb3"},       // 0x00B3
    {"szlig", "\xc3\x9f"},      // 0x00DF
    {"thinsp", "\xe2\x80\x89"}, // 0x2009
    {"thorn", "\xc3\xbe"},      // 0x00FE
    {"times", "\xc3\x97"},      // 0x00D7
    {"trade", "\xe2\x84\xa2"},  // 0x2122
    {"uacute", "\xc3\xba"},     // 0x00FA
    {"uarr", "\xe2\x86\x91"},   // 0x2191
    {"ucirc", "\xc3\xbb"},      // 0x00FB
    {"ugrave", "\xc3\xb9"},     // 0x00F9
    {"uml", "\xc2\xa8"},        // 0x00A8
    {"uuml", "\xc3\xbc"},       // 0x00FC
    {"yacute", "\xc3\xbd"},     // 0x00FD
    {"yen", "\xc2\xa5"},        // 0x00A5
    {"yuml", "\xc3\xbf"},       // 0x00FF
    {"omega", "\xcf\x89"},      // 0x03C9
};

static const size_t kNamedEntitiesCount =
    sizeof(kNamedEntities) / sizeof(kNamedEntities[0]);

// ============================================================================
// Transliteration Table (UTF-8 code point -> ASCII)
// ============================================================================

struct Transliteration {
    uint32_t code_point;
    const char *ascii;
};

// Sorted by code point. Binary search is overkill for this size; linear scan.
// Many contiguous ranges in Latin-1 (0xC0-0xFF), but we keep them explicit
// for clarity and to avoid off-by-one errors.
static const Transliteration kTransliterations[] = {
    // Whitespace
    {0x00A0, " "},     // nbsp
    {0x2000, " "},     // en quad
    {0x2001, " "},     // em quad
    {0x2002, " "},     // en space
    {0x2003, " "},     // em space
    {0x2004, " "},     // three-per-em space
    {0x2005, " "},     // four-per-em space
    {0x2006, " "},     // six-per-em space
    {0x2007, " "},     // figure space
    {0x2008, " "},     // punctuation space
    {0x2009, " "},     // thin space
    {0x200A, " "},     // hair space
    {0x200B, ""},      // zero-width space
    {0x00AD, ""},      // soft hyphen

    // Hyphens and dashes
    {0x2010, "-"},     // hyphen
    {0x2011, "-"},     // non-breaking hyphen
    {0x2012, "-"},     // figure dash
    {0x2013, "-"},     // en dash
    {0x2014, "--"},    // em dash
    {0x2015, "--"},    // horizontal bar

    // Quotes
    {0x2018, "'"},     // left single quote
    {0x2019, "'"},     // right single quote
    {0x201A, "'"},     // single low-9 quote
    {0x201B, "'"},     // single high-reversed-9 quote
    {0x201C, "\""},    // left double quote
    {0x201D, "\""},    // right double quote
    {0x201E, "\""},    // double low-9 quote
    {0x201F, "\""},    // double high-reversed-9 quote
    {0x2032, "'"},     // prime
    {0x2033, "\""},    // double prime

    // Guillemets and angle quotes
    {0x00AB, "<<"},    // left-pointing double angle quote
    {0x00BB, ">>"},    // right-pointing double angle quote
    {0x2039, "<"},     // single left-pointing angle quote
    {0x203A, ">"},     // single right-pointing angle quote

    // Ellipsis and bullet
    {0x2026, "..."},   // ellipsis
    {0x2022, "*"},     // bullet
    {0x2043, "-"},     // hyphen bullet
    {0x00B7, "."},     // middle dot

    // Symbols and abbreviations
    {0x00A9, "(c)"},   // copyright
    {0x00AE, "(R)"},   // registered
    {0x2122, "(tm)"},  // trademark
    {0x2120, "(sm)"},  // service mark

    // Math/science
    {0x00B0, " deg"},  // degree
    {0x00B1, "+/-"},   // plus-minus
    {0x00D7, "x"},     // multiplication
    {0x00F7, "/"},     // division
    {0x2212, "-"},     // minus sign
    {0x2260, "!="},    // not equal
    {0x2264, "<="},    // less-than or equal
    {0x2265, ">="},    // greater-than or equal
    {0x2248, "~"},     // approximately equal
    {0x221E, "inf"},   // infinity

    // Arrows
    {0x2190, "<-"},    // leftwards arrow
    {0x2192, "->"},    // rightwards arrow
    {0x2191, "^"},     // upwards arrow
    {0x2193, "v"},     // downwards arrow
    {0x2194, "<->"},   // left right arrow

    // Fractions and superscripts
    {0x00BD, "1/2"},   // one half
    {0x00BC, "1/4"},   // one quarter
    {0x00BE, "3/4"},   // three quarters
    {0x00B9, "1"},     // superscript one
    {0x00B2, "2"},     // superscript two
    {0x00B3, "3"},     // superscript three

    // Currency
    {0x00A2, "c"},     // cent
    {0x00A3, "GBP"},   // pound
    {0x00A5, "JPY"},   // yen
    {0x20AC, "EUR"},   // euro
    {0x00A4, "curr"},  // generic currency

    // Misc symbols
    {0x00A1, "!"},     // inverted exclamation
    {0x00BF, "?"},     // inverted question
    {0x00A7, "S"},     // section
    {0x00B6, "P"},     // pilcrow
    {0x2020, "+"},     // dagger
    {0x2021, "++"},    // double dagger
    {0x00AC, "~"},     // not sign
    {0x00A8, "\""},    // diaeresis
    {0x00AF, "-"},     // macron
    {0x00B4, "'"},     // acute accent
    {0x00B8, ","},     // cedilla

    // Latin-1 uppercase accents (0x00C0-0x00D6, 0x00D8)
    {0x00C0, "A"},     // Agrave
    {0x00C1, "A"},     // Aacute
    {0x00C2, "A"},     // Acirc
    {0x00C3, "A"},     // Atilde
    {0x00C4, "A"},     // Auml
    {0x00C5, "A"},     // Aring
    {0x00C6, "AE"},    // AElig
    {0x00C7, "C"},     // Ccedil
    {0x00C8, "E"},     // Egrave
    {0x00C9, "E"},     // Eacute
    {0x00CA, "E"},     // Ecirc
    {0x00CB, "E"},     // Euml
    {0x00CC, "I"},     // Igrave
    {0x00CD, "I"},     // Iacute
    {0x00CE, "I"},     // Icirc
    {0x00CF, "I"},     // Iuml
    {0x00D0, "D"},     // ETH
    {0x00D1, "N"},     // Ntilde
    {0x00D2, "O"},     // Ograve
    {0x00D3, "O"},     // Oacute
    {0x00D4, "O"},     // Ocirc
    {0x00D5, "O"},     // Otilde
    {0x00D6, "O"},     // Ouml
    {0x00D8, "O"},     // Oslash
    {0x00D9, "U"},     // Ugrave
    {0x00DA, "U"},     // Uacute
    {0x00DB, "U"},     // Ucirc
    {0x00DC, "U"},     // Uuml
    {0x00DD, "Y"},     // Yacute
    {0x00DE, "Th"},    // THORN

    // Latin-1 lowercase accents (0x00DF, 0x00E0-0x00F6, 0x00F8)
    {0x00DF, "ss"},    // szlig
    {0x00E0, "a"},     // agrave
    {0x00E1, "a"},     // aacute
    {0x00E2, "a"},     // acirc
    {0x00E3, "a"},     // atilde
    {0x00E4, "a"},     // auml
    {0x00E5, "a"},     // aring
    {0x00E6, "ae"},    // aelig
    {0x00E7, "c"},     // ccedil
    {0x00E8, "e"},     // egrave
    {0x00E9, "e"},     // eacute
    {0x00EA, "e"},     // ecirc
    {0x00EB, "e"},     // euml
    {0x00EC, "i"},     // igrave
    {0x00ED, "i"},     // iacute
    {0x00EE, "i"},     // icirc
    {0x00EF, "i"},     // iuml
    {0x00F0, "d"},     // eth
    {0x00F1, "n"},     // ntilde
    {0x00F2, "o"},     // ograve
    {0x00F3, "o"},     // oacute
    {0x00F4, "o"},     // ocirc
    {0x00F5, "o"},     // otilde
    {0x00F6, "o"},     // ouml
    {0x00F8, "o"},     // oslash
    {0x00F9, "u"},     // ugrave
    {0x00FA, "u"},     // uacute
    {0x00FB, "u"},     // ucirc
    {0x00FC, "u"},     // uuml
    {0x00FD, "y"},     // yacute
    {0x00FE, "th"},    // thorn
    {0x00FF, "y"},     // yuml

    // Extended Latin
    {0x0152, "OE"},    // OElig
    {0x0153, "oe"},    // oelig
    {0x0160, "S"},     // Scaron
    {0x0161, "s"},     // scaron
    {0x0178, "Y"},     // Ydieresis
    {0x017D, "Z"},     // Zcaron
    {0x017E, "z"},     // zcaron

    // Greek letters (minimal set, common in math/science)
    {0x03B1, "a"},     // alpha
    {0x03B2, "b"},     // beta
    {0x03B3, "g"},     // gamma
    {0x03B4, "d"},     // delta
    {0x03C0, "pi"},    // pi
    {0x03C3, "s"},     // sigma
    {0x03C9, "w"},     // omega
    {0x03BC, "u"},     // mu
    {0x03BB, "l"},     // lambda
    {0x0394, "D"},     // Delta
    {0x03A3, "S"},     // Sigma
    {0x03A9, "W"},     // Omega
};

static const size_t kTransliterationsCount =
    sizeof(kTransliterations) / sizeof(kTransliterations[0]);

// ============================================================================
// Utility Functions
// ============================================================================

// Encode a Unicode code point as UTF-8. Returns the number of bytes written
// (1-4), or 0 if the code point is invalid.
static size_t encode_utf8(uint32_t cp, unsigned char *out) {
    if (cp == 0 || cp > 0x10FFFF || (cp >= 0xD800 && cp <= 0xDFFF)) {
        return 0;  // Invalid
    }

    if (cp < 0x80) {
        out[0] = static_cast<unsigned char>(cp);
        return 1;
    }
    if (cp < 0x800) {
        out[0] = static_cast<unsigned char>(0xC0 | (cp >> 6));
        out[1] = static_cast<unsigned char>(0x80 | (cp & 0x3F));
        return 2;
    }
    if (cp < 0x10000) {
        out[0] = static_cast<unsigned char>(0xE0 | (cp >> 12));
        out[1] = static_cast<unsigned char>(0x80 | ((cp >> 6) & 0x3F));
        out[2] = static_cast<unsigned char>(0x80 | (cp & 0x3F));
        return 3;
    }

    out[0] = static_cast<unsigned char>(0xF0 | (cp >> 18));
    out[1] = static_cast<unsigned char>(0x80 | ((cp >> 12) & 0x3F));
    out[2] = static_cast<unsigned char>(0x80 | ((cp >> 6) & 0x3F));
    out[3] = static_cast<unsigned char>(0x80 | (cp & 0x3F));
    return 4;
}

// Decode one UTF-8 character from buf. Returns the code point, or 0xFFFD for
// an invalid sequence, and stores the bytes consumed in *out_len. Never reads
// past buf_size and always consumes at least one byte, so a caller looping on
// this cannot stall or run off the end of arbitrary binary input.
static uint32_t decode_utf8_char(const unsigned char *buf, size_t buf_size,
                                  size_t *out_len) {
    if (!buf || buf_size == 0) {
        if (out_len) *out_len = 0;
        return 0xFFFD;
    }

    unsigned char c0 = buf[0];

    if ((c0 & 0x80) == 0) {
        // 1-byte sequence (ASCII)
        if (out_len) *out_len = 1;
        return c0;
    }

    if ((c0 & 0xE0) == 0xC0) {
        // 2-byte sequence
        if (buf_size < 2) {
            if (out_len) *out_len = 1;
            return 0xFFFD;  // Truncated
        }
        unsigned char c1 = buf[1];
        if ((c1 & 0xC0) != 0x80) {
            if (out_len) *out_len = 1;
            return 0xFFFD;
        }
        uint32_t cp = (((uint32_t)c0 & 0x1F) << 6) | ((uint32_t)c1 & 0x3F);
        if (cp < 0x80) {
            // Overlong
            if (out_len) *out_len = 1;
            return 0xFFFD;
        }
        if (out_len) *out_len = 2;
        return cp;
    }

    if ((c0 & 0xF0) == 0xE0) {
        // 3-byte sequence
        if (buf_size < 3) {
            if (out_len) *out_len = 1;
            return 0xFFFD;  // Truncated
        }
        unsigned char c1 = buf[1];
        unsigned char c2 = buf[2];
        if ((c1 & 0xC0) != 0x80 || (c2 & 0xC0) != 0x80) {
            if (out_len) *out_len = 1;
            return 0xFFFD;
        }
        uint32_t cp = (((uint32_t)c0 & 0x0F) << 12) | (((uint32_t)c1 & 0x3F) << 6)
                    | ((uint32_t)c2 & 0x3F);
        if (cp < 0x800) {
            // Overlong
            if (out_len) *out_len = 1;
            return 0xFFFD;
        }
        if (out_len) *out_len = 3;
        return cp;
    }

    if ((c0 & 0xF8) == 0xF0) {
        // 4-byte sequence
        if (buf_size < 4) {
            if (out_len) *out_len = 1;
            return 0xFFFD;  // Truncated
        }
        unsigned char c1 = buf[1];
        unsigned char c2 = buf[2];
        unsigned char c3 = buf[3];
        if ((c1 & 0xC0) != 0x80 || (c2 & 0xC0) != 0x80 || (c3 & 0xC0) != 0x80) {
            if (out_len) *out_len = 1;
            return 0xFFFD;
        }
        uint32_t cp = (((uint32_t)c0 & 0x07) << 18) | (((uint32_t)c1 & 0x3F) << 12)
                    | (((uint32_t)c2 & 0x3F) << 6) | ((uint32_t)c3 & 0x3F);
        if (cp < 0x10000 || cp > 0x10FFFF) {
            // Overlong or out of range
            if (out_len) *out_len = 1;
            return 0xFFFD;
        }
        if (out_len) *out_len = 4;
        return cp;
    }

    // Invalid start byte
    if (out_len) *out_len = 1;
    return 0xFFFD;
}

// Look up code point in transliteration table (linear scan).
static const char *transliterate(uint32_t cp) {
    for (size_t i = 0; i < kTransliterationsCount; i++) {
        if (kTransliterations[i].code_point == cp) {
            return kTransliterations[i].ascii;
        }
    }
    return nullptr;
}

// Look up named entity (linear scan, case-sensitive).
static const char *lookup_entity(const char *name, size_t len) {
    for (size_t i = 0; i < kNamedEntitiesCount; i++) {
        const char *ent_name = kNamedEntities[i].name;
        if (std::strlen(ent_name) == len &&
            std::memcmp(ent_name, name, len) == 0) {
            return kNamedEntities[i].utf8;
        }
    }
    return nullptr;
}

}  // namespace

// ============================================================================
// Public API
// ============================================================================

std::string fn_decode_entities(const std::string &in) {
    std::string out;
    out.reserve(in.size());

    for (size_t i = 0; i < in.size(); ++i) {
        if (in[i] == '&') {
            // Look for entity terminator within a reasonable distance
            size_t end = in.find(';', i + 1);
            if (end == std::string::npos || end - i > 10) {
                // No terminator or too far; pass through verbatim
                out += '&';
                continue;
            }

            std::string entity_body = in.substr(i + 1, end - i - 1);

            if (entity_body.empty()) {
                // Just "&;"
                out += '&';
                continue;
            }

            if (entity_body[0] == '#') {
                // Numeric entity: &#...;
                uint32_t cp = 0;
                bool valid = false;

                if (entity_body.size() > 1 &&
                    (entity_body[1] == 'x' || entity_body[1] == 'X')) {
                    // Hexadecimal: &#xHHHH;
                    const char *hex_str = entity_body.c_str() + 2;
                    char *end_ptr = nullptr;
                    long val = std::strtol(hex_str, &end_ptr, 16);
                    if (end_ptr != hex_str && *end_ptr == '\0' && val > 0 &&
                        val <= 0x10FFFF && !(val >= 0xD800 && val <= 0xDFFF)) {
                        cp = static_cast<uint32_t>(val);
                        valid = true;
                    }
                } else {
                    // Decimal: &#DDDD;
                    const char *dec_str = entity_body.c_str() + 1;
                    char *end_ptr = nullptr;
                    long val = std::strtol(dec_str, &end_ptr, 10);
                    if (end_ptr != dec_str && *end_ptr == '\0' && val > 0 &&
                        val <= 0x10FFFF && !(val >= 0xD800 && val <= 0xDFFF)) {
                        cp = static_cast<uint32_t>(val);
                        valid = true;
                    }
                }

                if (valid) {
                    // Encode as UTF-8
                    unsigned char utf8_buf[4];
                    size_t utf8_len = encode_utf8(cp, utf8_buf);
                    if (utf8_len > 0) {
                        out.append(reinterpret_cast<const char *>(utf8_buf),
                                   utf8_len);
                        i = end;  // Skip past the semicolon
                        continue;
                    }
                }

                // Invalid numeric entity; pass through
                out += '&';
                continue;

            } else {
                // Named entity: &name;
                const char *decoded = lookup_entity(entity_body.c_str(),
                                                     entity_body.size());
                if (decoded) {
                    out += decoded;
                    i = end;  // Skip past the semicolon
                    continue;
                }

                // Unknown entity; pass through verbatim
                out += '&';
                continue;
            }
        } else {
            out += in[i];
        }
    }

    return out;
}

std::string fn_utf8_to_ascii(const std::string &in) {
    std::string out;
    out.reserve(in.size());

    const unsigned char *data =
        reinterpret_cast<const unsigned char *>(in.data());
    size_t remaining = in.size();

    while (remaining > 0) {
        size_t char_len = 0;
        uint32_t cp = decode_utf8_char(data, remaining, &char_len);

        if (char_len == 0) {
            // Should not happen, but be safe
            data++;
            remaining--;
            continue;
        }

        data += char_len;
        remaining -= char_len;

        // Handle special cases
        if (cp == '\t' || cp == '\n' || cp == '\r') {
            out += ' ';
            continue;
        }

        // Printable ASCII
        if (cp >= 0x20 && cp <= 0x7E) {
            out += static_cast<char>(cp);
            continue;
        }

        // Look up transliteration
        const char *translit = transliterate(cp);
        if (translit) {
            out += translit;
            continue;
        }

        // Drop everything else (including invalid sequences)
    }

    return out;
}

std::string fn_sanitize_ascii(const std::string &in) {
    return fn_utf8_to_ascii(fn_decode_entities(in));
}
