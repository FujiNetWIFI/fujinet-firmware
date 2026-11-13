/**
 * HTML Wrapper for #FujiNet
 *
 * Parses a (possibly malformed) HTML document with Gumbo (HTML5 tree
 * construction, robust error recovery) and resolves a CSS selector query via
 * gumbo-query, returning the text (or an attribute) of the match(es).
 * Deliberately mirrors the FNJSON interface so device network handlers can
 * drive it the same way they drive JSON.
 */

#ifndef FNHTML_H
#define FNHTML_H

#include <string.h>
#include <string>

#include "../fntext/fn_query_flags.h"
#include "../fnpretty/fnpretty.h"
#include "../network-protocol/Protocol.h"

class CDocument; // gumbo-query

enum HTMLQueryFlags_t {
    HTML_REMAP_CHARS = FN_QUERY_REMAP_CHARS,
    HTML_REMAP_ATASCII_INTERNATIONAL = FN_QUERY_REMAP_ATASCII_INTERNATIONAL,
};

class FNHTML
{
public:
    FNHTML();
    virtual ~FNHTML();

    void setLineEnding(const std::string &_lineEnding);
    void setProtocol(NetworkProtocol *newProtocol);
    void setReadQuery(const std::string &queryString, uint8_t queryParam);
    bool status(NetworkStatus *status);

    bool parse();
    int readValueLen();
    bool readValue(uint8_t *buf, unsigned short len);
    std::string processString(std::string in);
    void setQueryParam(uint8_t qp);
    uint8_t queryParam() const { return _queryParam; }
    // Wrap width in columns for FN_QUERY_OUTPUT_TEXT; 0 = default (40).
    void setTextWidth(uint8_t width) { _textWidth = width; }
    uint8_t textWidth() const { return _textWidth; }
    size_t available() { return _html_bytes_remaining; }
    // The last query's match as it is in the document (text or attribute), with no
    // output mode or remapping. False if no query has been made since parse();
    // empty if it matched nothing.
    bool rawValue(std::string &out);

private:
    CDocument *_doc = nullptr;
    NetworkProtocol *_protocol = nullptr;
    std::string _queryString;
    uint8_t _queryParam = 0;
    uint8_t _textWidth = 0;
    std::string lineEnding;
    std::string _parseBuffer;

    // Match iteration: repeating a query with the same selector advances to the
    // next match; a different selector (or a fresh parse) restarts at the first.
    std::string _lastQuery;
    size_t _matchIndex = 0;
    bool _hasQuery = false;

    // Result of the last resolved query, plus how many of its bytes are still
    // to be handed to the client.
    std::string _value;
    int _html_bytes_remaining = 0;

    bool matchValue(std::string &v);
    void resolveQuery();
};

#endif /* FNHTML_H */
