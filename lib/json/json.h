/**
 * JSON Parser wrapper for #FujiNet
 * 
 * Thom Cherryhomes
 *   <thom.cherryhomes@gmail.com>
 */

#ifndef JSON_H
#define JSON_H

#include <networkProtocol.h>
#include <cJSON.h>

class JSON
{
public:
    JSON();
    virtual ~JSON();

    void setProtocol(networkProtocol *newProtocol);
    void setReadQuery(string queryString);
    cJSON *resolveQuery();
    
    bool parse();
    int readValueLen();
    bool readValue(uint8_t *buf, unsigned short len);

private:
    cJSON *_json;
    networkProtocol *_protocol;
    string _queryString;

    string getValue(cJSON *item);
    
};

#endif /* JSON_H */