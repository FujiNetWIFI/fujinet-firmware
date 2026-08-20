/**
 * JSON Parser wrapper for #FujiNet
 *
 * Thom Cherryhomes
 *   <thom.cherryhomes@gmail.com>
 */

#ifndef JSON_H
#define JSON_H

#include <cJSON.h>
#include <cJSON_Utils.h>
#include <string.h>

#include "../network-protocol/Protocol.h"

enum JSONQueryFlags_t {
    // Low nibble: existing per-value flags.
    JSON_REMAP_CHARS = 0x01,
    JSON_REMAP_ATASCII_INTERNATIONAL = 0x02,
    JSON_DELETE_SGML_TAGS = 0x04,

    // Bits 4-5: how a queried value is post-processed before it is returned.
    // Kept numerically identical to HTML_OUTPUT_* / XML_OUTPUT_* so a client
    // uses the same bits whichever query mode it is in. VERBATIM is 0, so a
    // client that sends no output mode keeps the historical behavior exactly.
    JSON_OUTPUT_MASK = 0x30,
    JSON_OUTPUT_VERBATIM = 0x00, // bytes exactly as the document had them
    JSON_OUTPUT_ASCII = 0x10,    // decode entities, transliterate, strip non-ASCII
    // 0x20 and 0x30 are reserved for future output modes.
};

class FNJSON
{
public:
    FNJSON();
    virtual ~FNJSON();

    void setLineEnding(const std::string &_lineEnding);
    void setProtocol(NetworkProtocol *newProtocol);
    void setReadQuery(const std::string &queryString, uint8_t queryParam);
    cJSON *resolveQuery();
    bool status(NetworkStatus *status);

    bool parse();
    int readValueLen();
    bool readValue(uint8_t *buf, unsigned short len);
    std::string processString(std::string in);
    void setQueryParam(uint8_t qp);
    size_t available() { return _json_bytes_remaining; }

private:
    cJSON *_json = nullptr;
    cJSON *_item = nullptr;
    NetworkProtocol *_protocol = nullptr;
    std::string _queryString;
    uint8_t _queryParam = 0;
    std::string lineEnding;
    std::string getValue(cJSON *item);
    std::string _parseBuffer;
    int _json_bytes_remaining = 0;
};

#endif /* JSON_H */
