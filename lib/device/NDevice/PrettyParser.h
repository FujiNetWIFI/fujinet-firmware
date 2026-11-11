#ifndef PRETTYPARSER_H
#define PRETTYPARSER_H

#include "NParser.h"
#include "fnpretty.h"
#include "BusCharset.h"

/**
 * PARSER::PRETTY: the whole document, rendered to word-wrapped plain text by
 * FNPretty and read back sequentially. Unlike the query parsers there is no
 * "one value per query": PARSE fetches and renders the page, then the client
 * runs STATUS/READ until it is drained.
 *
 * NET_QUERY payloads, by form:
 *   all digits  1-based link index; the link's URL + EOL is staged and served
 *               by the next read(s) in place of page text, after which the page
 *               resumes where it left off
 *   "sel@attr"  value of attribute attr on the first element matching the CSS
 *               selector sel (last '@' outside quotes and [...]; attr is
 *               [A-Za-z0-9_:-]+), raw but ASCII-folded; staged + EOL and served
 *               exactly like a link URL, page and section untouched. No match
 *               or no such attribute is an error (END_OF_FILE)
 *   anything    CSS selector naming the section to show (empty = whole page),
 *               then re-render
 */
class PrettyParser : public NParser
{
protected:
    FNPretty _pretty;
    std::string _eol;
    bool _eolExplicit = false;

    // A link URL staged by a numeric query, served instead of the page.
    std::string _staged;
    bool _linkPending = false;

    void applyDefaults();

public:
    PrettyParser(NetworkProtocol *protocol);

    fujiError_t read(std::string &buffer, size_t length) override;
    fujiError_t write(std::string &buffer) override;
    size_t available() override;
    off_t seek(off_t offset, int whence) override;

    error_is_true setQuery(const std::string &query) override;
    error_is_true parse() override;
    error_is_true setQueryParam(uint8_t param) override;
    error_is_true setLineEnding(const std::string &eol) override;
    error_is_true setTextWidth(uint8_t width) override;
    error_is_true setRenderLinks(bool on) override;
    error_is_true setNativeGraphics(bool on) override;

    NetworkStatus status() override;
};

#endif /* PRETTYPARSER_H */
