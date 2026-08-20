/**
 * Text sanitization helpers for #FujiNet query results.
 *
 * Shared by the N: device query wrappers (FNHTML, FNXML) to turn arbitrary
 * web text into something an 8-bit host can print. Deliberately free of any
 * parser/protocol dependency so it can be unit tested on its own.
 *
 * The ESP firmware builds with C++ exceptions and RTTI-sensitive constructs
 * avoided: no throw/try/catch, no std::regex, tables only.
 */

#ifndef FN_SANITIZE_H
#define FN_SANITIZE_H

#include <string>

/**
 * Decode HTML/XML character entities into their UTF-8 encoding.
 *
 * Handles numeric references (&#8212; and &#x2014;) plus a curated table of
 * named entities (&amp; &nbsp; &mdash; &rsquo; &eacute; ...). The full HTML5
 * named-entity set is ~2200 entries and far too large for ESP flash, so the
 * table covers what actually shows up in web text and feeds.
 *
 * An unrecognized entity is left in place verbatim (&foo; stays &foo;) rather
 * than being dropped, so nothing silently disappears.
 */
std::string fn_decode_entities(const std::string &in);

/**
 * Fold UTF-8 text down to printable ASCII (0x20-0x7E).
 *
 * Characters with a reasonable ASCII spelling are transliterated (em dash ->
 * "--", curly quotes -> "\"" and "'", ellipsis -> "...", accented Latin
 * letters -> their base letter, "(c)" / "(R)" / "(tm)", ...). Tab, CR and LF
 * become a single space so words cannot run together when the newlines are
 * stripped. Everything else outside printable ASCII -- including invalid or
 * truncated UTF-8 -- is removed.
 */
std::string fn_utf8_to_ascii(const std::string &in);

/**
 * fn_utf8_to_ascii(fn_decode_entities(in)) -- the full sanitize pass used by
 * the *_OUTPUT_ASCII query option.
 */
std::string fn_sanitize_ascii(const std::string &in);

#endif /* FN_SANITIZE_H */
