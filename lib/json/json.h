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
    bool parse();

private:
    cJSON *_json;
    networkProtocol *_protocol;

};

#endif /* JSON_H */