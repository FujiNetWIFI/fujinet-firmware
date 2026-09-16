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

#include "../network-protocol/Protocol.h"

class CDocument; // gumbo-query

enum HTMLQueryFlags_t {
    HTML_REMAP_CHARS = 0x01,
    HTML_REMAP_ATASCII_INTERNATIONAL = 0x02,
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
    size_t available() { return _html_bytes_remaining; }

private:
    CDocument *_doc = nullptr;
    NetworkProtocol *_protocol = nullptr;
    std::string _queryString;
    uint8_t _queryParam = 0;
    std::string lineEnding;
    std::string _parseBuffer;

    // Match iteration: repeating a query with the same selector advances to the
    // next match; a different selector (or a fresh parse) restarts at the first.
    std::string _lastQuery;
    size_t _matchIndex = 0;

    // Result of the last resolved query, plus how many of its bytes are still
    // to be handed to the client.
    std::string _value;
    int _html_bytes_remaining = 0;

    void resolveQuery();
};

#endif /* FNHTML_H */
