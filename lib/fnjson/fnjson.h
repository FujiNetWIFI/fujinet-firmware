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

#include "Protocol.h"

class FNJSON
{
public:
    FNJSON();
    virtual ~FNJSON();

    void setLineEnding(string _lineEnding);
    void setProtocol(NetworkProtocol *newProtocol);
    void setReadQuery(string queryString, uint8_t queryParam);
    cJSON *resolveQuery();
    bool status(NetworkStatus *status);
    
    bool parse();
    int readValueLen();
    bool readValue(uint8_t *buf, unsigned short len);
    string processString(string in);
    int json_bytes_remaining;
    
private:
    cJSON *_json;
    cJSON *_item;
    NetworkProtocol *_protocol;
    string _queryString;
    uint8_t _queryParam;
    string lineEnding;
    string getValue(cJSON *item);
    string _parseBuffer;
};

#endif /* JSON_H */