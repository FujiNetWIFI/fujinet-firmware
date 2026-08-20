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

#include "../network-protocol/Protocol.h"

namespace tinyxml2 { class XMLDocument; class XMLElement; }

enum XMLQueryFlags_t {
    // Low nibble: character-remapping flags (unchanged).
    XML_REMAP_CHARS = 0x01,
    XML_REMAP_ATASCII_INTERNATIONAL = 0x02,

    // Bits 4-5: how a queried value is post-processed before it is returned.
    // A field rather than a single flag so further modes can be added without
    // disturbing the low-nibble flags. VERBATIM is 0, so a client that sends
    // no output mode keeps the historical behavior exactly.
    XML_OUTPUT_MASK = 0x30,
    XML_OUTPUT_VERBATIM = 0x00, // bytes exactly as the document had them
    XML_OUTPUT_ASCII = 0x10,    // decode entities, transliterate, strip non-ASCII
    // 0x20 and 0x30 are reserved for future output modes.
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
