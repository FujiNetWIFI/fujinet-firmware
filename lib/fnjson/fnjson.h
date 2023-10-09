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

#include "../network-protocol/Protocol.h"

class FNJSON
{
public:
    FNJSON();
    virtual ~FNJSON();

    void setLineEnding(const string &_lineEnding);
    void setProtocol(NetworkProtocol *newProtocol);
    void setReadQuery(const string &queryString, uint8_t queryParam);
    cJSON *resolveQuery();
    bool status(NetworkStatus *status);
    
    bool parse();
    int readValueLen();
    bool readValue(uint8_t *buf, unsigned short len);
    string processString(string in);
    int json_bytes_remaining = 0;
    void setQueryParam(uint8_t qp);
    
private:
    cJSON *_json = nullptr;
    cJSON *_item = nullptr;
    NetworkProtocol *_protocol = nullptr;
    string _queryString;
    uint8_t _queryParam = 0;
    string lineEnding;
    string getValue(cJSON *item);
    string _parseBuffer;
};

#endif /* JSON_H */