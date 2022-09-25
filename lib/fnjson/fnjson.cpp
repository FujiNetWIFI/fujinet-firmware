/**
 * JSON Wrapper for #FujiNet
 *
 * Thomas Cherryhomes
 *   <thom.cherryhomes@gmail.com>
 */

#include "fnjson.h"

#include <string.h>
#include <sstream>
#include <math.h>
#include <iomanip>

#include "../../include/debug.h"

/**
 * ctor
 */
FNJSON::FNJSON()
{
    Debug_printf("FNJSON::ctor()\n");
    _protocol = nullptr;
    _json = nullptr;
}

/**
 * dtor
 */
FNJSON::~FNJSON()
{
    Debug_printf("FNJSON::dtor()\n");
    _protocol = nullptr;
    _json = nullptr;
}

/**
 * Specify line ending
 */
void FNJSON::setLineEnding(string _lineEnding)
{
    lineEnding = _lineEnding;
}

/**
 * Attach protocol handler
 */
void FNJSON::setProtocol(NetworkProtocol *newProtocol)
{
    Debug_printf("FNJSON::setProtocol()\n");
    _protocol = newProtocol;
}

/**
 * Set read query string
 */
void FNJSON::setReadQuery(string queryString, uint8_t queryParam)
{
    _queryString = queryString;
    _queryParam = queryParam;
    _item = resolveQuery();
    json_bytes_remaining=readValueLen();
}

/**
 * Resolve query string
 */
cJSON *FNJSON::resolveQuery()
{
    if (_queryString.empty())
        return _json;

    return cJSONUtils_GetPointer(_json, _queryString.c_str());
}

/**
 * Process string, strip out HTML tags if needed
 */
string FNJSON::processString(string in)
{
    while (in.find("<") != string::npos)
    {
        auto startpos = in.find("<");
        auto endpos = in.find(">") + 1;

        if (endpos != string::npos)
        {
            in.erase(startpos,endpos-startpos);
        }

    }
    return in;
}

/**
 * Return normalized string of JSON item
 */
string FNJSON::getValue(cJSON *item)
{
    if (cJSON_IsString(item))
    {
        stringstream ss;

        Debug_printf("S: [cJSON_IsString] %s\n",cJSON_GetStringValue(item));

        //ss << string(cJSON_GetStringValue(item));
        ss << cJSON_GetStringValue(item);

        return processString(ss.str() + lineEnding);
    }
    else if (cJSON_IsBool(item))
    {
        Debug_printf("S: [cJSON_IsBool] %s\n",cJSON_IsTrue(item) ? "true" : "false");

        if (cJSON_IsTrue(item))
            return "TRUE" + lineEnding;
        else if (cJSON_IsFalse(item))
            return "FALSE" + lineEnding;
    }
    else if (cJSON_IsNull(item))
    {
        Debug_printf("S: [cJSON_IsNull]\n");

        return "NULL" + lineEnding;
    }
    else if (cJSON_IsNumber(item))
    {
        stringstream ss;

        Debug_printf("S: [cJSON_IsNumber] %f\n",cJSON_GetNumberValue(item));

        // Is the number an integer?
        if (floor(cJSON_GetNumberValue(item)) == cJSON_GetNumberValue(item))
        {
            // yes, return as 64 bit integer
            ss << (int64_t)(cJSON_GetNumberValue(item));
        }
        else
        {
            // no, return as double with max. 10 digits
            ss << setprecision(10)<<cJSON_GetNumberValue(item);
        }

        return ss.str() + lineEnding;
    }
    else if (cJSON_IsObject(item))
    {
        string ret = "";

        item = item->child;

        do
        {
            ret += string(item->string) + lineEnding + getValue(item);
        } while ((item = item->next) != NULL);

        return ret;
    }
    else if (cJSON_IsArray(item))
    {
        cJSON *child = item->child;
        string ret;

        do
        {
            ret += getValue(child);
        } while ((child = child->next) != NULL);

        return ret;
    }

    return "UNKNOWN" + lineEnding;
}

/**
 * Return requested value
 */
bool FNJSON::readValue(uint8_t *rx_buf, unsigned short len)
{
    if (_item == nullptr)
        return true; // error

    string ret = getValue(_item);

    memcpy(rx_buf, ret.data(), len);

    return false; // no error.
}

/**
 * Return requested value length
 */
int FNJSON::readValueLen()
{
    if (_item == nullptr)
        return 0;

    return getValue(_item).size();
}

/**
 * Parse data from protocol
 */
bool FNJSON::parse()
{
    NetworkStatus ns;
    _parseBuffer.clear();

    if (_protocol == nullptr)
    {
        Debug_printf("FNJSON::parse() - NULL protocol.\n");
        return false;
    }

    _protocol->status(&ns);

    while (ns.connected)
    {
        _protocol->read(ns.rxBytesWaiting);
        _parseBuffer += *_protocol->receiveBuffer;
        _protocol->receiveBuffer->clear();
        _protocol->status(&ns);
        vTaskDelay(10);
    }

    Debug_printf("S: %s\n",_parseBuffer.c_str());
    _json = cJSON_Parse(_parseBuffer.c_str());

    if (_json == nullptr)
    {
        Debug_printf("FNJSON::parse() - Could not parse JSON\n");
        return false;
    }

    Debug_printf("Parsed JSON: %s\n", cJSON_Print(_json));

    return true;
}

bool FNJSON::status(NetworkStatus *s)
{
    Debug_printf("FNJSON::status(%u) %s\n",json_bytes_remaining,getValue(_item).c_str());
    s->connected = true;
    s->rxBytesWaiting = json_bytes_remaining;
    s->error = json_bytes_remaining == 0 ? 136 : 0;
    return false;
}