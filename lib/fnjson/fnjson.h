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
    void setReadQuery(string queryString);
    cJSON *resolveQuery();
    
    bool parse();
    int readValueLen();
    bool readValue(uint8_t *buf, unsigned short len);
    string processString(string in);

private:
    cJSON *_json;
    cJSON *_item;
    NetworkProtocol *_protocol;
    string _queryString;
    string lineEnding;
    string getValue(cJSON *item);
    string _parseBuffer;
};

#endif /* JSON_H */