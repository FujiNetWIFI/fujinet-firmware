/**
 * JSON Wrapper for #FujiNet
 *
 * Thomas Cherryhomes
 *   <thom.cherryhomes@gmail.com>
 */

#include "fnjson.h"

#include <string.h>
#include <sstream>

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
void FNJSON::setReadQuery(string queryString)
{
    _queryString = queryString;
    _item = resolveQuery();
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
        return processString(string(cJSON_GetStringValue(item)) + lineEnding);
    else if (cJSON_IsBool(item))
    {
        if (cJSON_IsTrue(item))
            return "TRUE" + lineEnding;
        else if (cJSON_IsFalse(item))
            return "FALSE" + lineEnding;
    }
    else if (cJSON_IsNull(item))
        return "NULL" + lineEnding;
    else if (cJSON_IsNumber(item))
    {
        stringstream ss;
        ss << item->valuedouble;
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
    string ret = getValue(_item);

    if (_item == nullptr)
        return true; // error

    memcpy(rx_buf, ret.data(), ret.size());

    return false; // no error.
}

/**
 * Return requested value length
 */
int FNJSON::readValueLen()
{
    int len = getValue(_item).size();

    if (_item == nullptr)
        return len;

    return len;
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