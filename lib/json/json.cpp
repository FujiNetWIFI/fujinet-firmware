/**
 * JSON Wrapper for #FujiNet
 * 
 * Thomas Cherryhomes
 *   <thom.cherryhomes@gmail.com>
 */

#include "json.h"

/**
 * ctor
 */
JSON::JSON()
{
    _protocol = nullptr;
}

/**
 * dtor
 */
JSON::~JSON()
{
    _protocol = nullptr;
}

/**
 * Attach protocol handler
 */
void JSON::setProtocol(networkProtocol *newProtocol)
{
    _protocol=newProtocol;
}