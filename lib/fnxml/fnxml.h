/**
 * XML Wrapper for #FujiNet
 *
 * Parses a well-formed XML document with tinyxml2 and resolves an XPath-subset
 * query, returning the text (or an attribute) of the match(es).
 *
 * This is the strict-XML counterpart to FNHTML: Gumbo/gumbo-query does HTML5
 * tree construction with error recovery and CSS selectors, which is the wrong
 * model for XML (namespaced/mixed-case element names, no implied tags, no HTML
 * element vocabulary). FNXML deliberately mirrors the FNHTML/FNJSON interface
 * so device network handlers can drive it the same way.
 */

#ifndef FNXML_H
#define FNXML_H

#include <string.h>
#include <string>

#include "../fntext/fn_query_flags.h"
#include "../network-protocol/Protocol.h"

namespace tinyxml2 { class XMLDocument; class XMLElement; }

enum XMLQueryFlags_t {
    XML_REMAP_CHARS = FN_QUERY_REMAP_CHARS,
    XML_REMAP_ATASCII_INTERNATIONAL = FN_QUERY_REMAP_ATASCII_INTERNATIONAL,
};

class FNXML
{
public:
    FNXML();
    virtual ~FNXML();

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
    size_t available() { return _xml_bytes_remaining; }

private:
    tinyxml2::XMLDocument *_doc = nullptr;
    NetworkProtocol *_protocol = nullptr;
    std::string _queryString;
    uint8_t _queryParam = 0;
    std::string lineEnding;
    std::string _parseBuffer;

    // Match iteration: repeating a query with the same path advances to the
    // next match; a different path (or a fresh parse) restarts at the first.
    std::string _lastQuery;
    size_t _matchIndex = 0;

    // Result of the last resolved query, plus how many of its bytes are still
    // to be handed to the client.
    std::string _value;
    int _xml_bytes_remaining = 0;

    void resolveQuery();
};

#endif /* FNXML_H */
