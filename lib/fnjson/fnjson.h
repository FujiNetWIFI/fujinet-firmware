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
