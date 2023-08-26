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
#include <ostream>
#include "string_utils.h"
#include "../../include/debug.h"

/**
 * ctor
 */
FNJSON::FNJSON()
{
    Debug_printf("FNJSON::ctor()\r\n");
    _protocol = nullptr;
    _json = nullptr;
}

/**
 * dtor
 */
FNJSON::~FNJSON()
{
    Debug_printf("FNJSON::dtor()\r\n");
    _protocol = nullptr;
    if (_json != nullptr)
        cJSON_Delete(_json);
    _json = nullptr;
}

/**
 * Specify line ending
 */
void FNJSON::setLineEnding(const string &_lineEnding)
{
    lineEnding = _lineEnding;
}

/**
 * Attach protocol handler
 */
void FNJSON::setProtocol(NetworkProtocol *newProtocol)
{
    Debug_printf("FNJSON::setProtocol()\r\n");
    _protocol = newProtocol;
}

/**
 * Set read query string
 */
void FNJSON::setReadQuery(const string &queryString, uint8_t queryParam)
{
    _queryString = queryString;
    _queryParam = queryParam;
    _item = resolveQuery();
    json_bytes_remaining = readValueLen();
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
            in.erase(startpos, endpos - startpos);
        }
    }

#ifdef BUILD_IEC
    mstr::toPETSCII(in);
#endif

#ifdef BUILD_ATARI
    // SIO AUX bits 0+1 control the mapping
    //   Bit 0=0 - don't touch the characters
    //   Bit 0=1 - convert the characters when possible
    //   Bit 1=0 - convert to generic ASCII/ATASCII (no font change needed)
    //   Bit 1=1 - convert to ATASCII international charset (need to be switched on ATARI, i.e via POKE 756,204)

    // SIO AUX2 Bit 1 set?
    if ((_queryParam & 1) != 0)
    {
        // yes, map special characters
        Debug_printf("S: [Mapping->ATARI]\r\n");

        // SIO AUX2 Bit 2 set?
        if ((_queryParam & 2) != 0)
        {
            // yes, mapping to international charset
            string mapFrom[] = {"á", "ù", "Ñ", "É", "ç", "ô", "ò", "ì", "£", "ï", "ü", "ä", "Ö", "ú", "ó", "ö", "Ü", "â", "û", "î", "é", "è", "ñ", "ê", "å", "à", "Å", "¡", "Ä", "ß"};
            string mapTo[] = {"\x00", "\x01", "\x02", "\x03", "\x04", "\x05", "\x06", "\x07", "\x08", "\x09", "\x0a", "\x0b", "\x0c", "\x0d", "\x0e", "\x0f", "\x10", "\x11", "\x12", "\x13", "\x14", "\x15", "\x16", "\x17", "\x18", "\x19", "\x1a", "\x60", "\x7b", "ss"};
            int elementCount = sizeof(mapFrom) / sizeof(mapFrom[0]);
            for (int elementIndex = 0; elementIndex < elementCount; elementIndex++)
                if (in.find(mapFrom[elementIndex]) != std::string::npos)
                    in.replace(in.find(mapFrom[elementIndex]), string(mapFrom[elementIndex]).size(), mapTo[elementIndex]);
        }
        else
        {
            // no, mapping to normal ASCI (workaround)
            string mapFrom[] = {"Ä", "Ö", "Ü", "ä", "ö", "ü", "ß", "é", "è", "á", "à", "ó", "ò", "ú", "ù"};
            string mapTo[] = {"Ae", "Oe", "Ue", "ae", "oe", "ue", "ss", "e", "e", "a", "a", "o", "o", "u", "u"};
            int elementCount = sizeof(mapFrom) / sizeof(mapFrom[0]);
            for (int elementIndex = 0; elementIndex < elementCount; elementIndex++)
                if (in.find(mapFrom[elementIndex]) != std::string::npos)
                    in.replace(in.find(mapFrom[elementIndex]), string(mapFrom[elementIndex]).size(), mapTo[elementIndex]);
        }

    }
#endif

    return in;
}

/**
 * Return normalized string of JSON item
 */
string FNJSON::getValue(cJSON *item)
{
    stringstream ss;

    if (cJSON_IsString(item))
    {
        Debug_printf("S: [cJSON_IsString] %s\r\n", cJSON_GetStringValue(item));
        ss << processString(cJSON_GetStringValue(item) + lineEnding);
    }
    else if (cJSON_IsBool(item))
    {
        Debug_printf("S: [cJSON_IsBool] %s\r\n", cJSON_IsTrue(item) ? "true" : "false");

        if (cJSON_IsTrue(item))
            ss << "TRUE" + lineEnding;
        else if (cJSON_IsFalse(item))
            ss << "FALSE" + lineEnding;
    }
    else if (cJSON_IsNull(item))
    {
        Debug_printf("S: [cJSON_IsNull]\r\n");

        ss << "NULL" + lineEnding;
    }
    else if (cJSON_IsNumber(item))
    {
        Debug_printf("S: [cJSON_IsNumber] %f\r\n", cJSON_GetNumberValue(item));

        // Is the number an integer?
        if (floor(cJSON_GetNumberValue(item)) == cJSON_GetNumberValue(item))
        {
            // yes, return as 64 bit integer
            ss << (int64_t)(cJSON_GetNumberValue(item));
        }
        else
        {
            // no, return as double with max. 10 digits
            ss << setprecision(10) << cJSON_GetNumberValue(item);
        }

        ss << lineEnding;
    }
    else if (cJSON_IsObject(item))
    {
        #ifdef BUILD_IEC
            // Set line ending when returning multiple values
            setLineEnding("\x0a");
        #endif

        item = item->child;

        do
        {
            #ifdef BUILD_IEC
                // Convert key to PETSCII
                string tempStr = string((const char *)item->string);
                mstr::toPETSCII(tempStr);
                ss << tempStr;
            #else
                ss << item->string;
            #endif

            ss << lineEnding + getValue(item);
        } while ((item = item->next) != NULL);
    }
    else if (cJSON_IsArray(item))
    {
        cJSON *child = item->child;
        do
        {
            ss << getValue(child);
        } while ((child = child->next) != NULL);
    }
    else
        ss << "UNKNOWN" + lineEnding;
  
    return ss.str();
}

/**
 * Return requested value
 */
bool FNJSON::readValue(uint8_t *rx_buf, unsigned short len)
{    
    if (_item == nullptr)
        return true; // error

    memcpy(rx_buf, getValue(_item).data(), len);

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

    if (_json != nullptr)
        cJSON_Delete(_json);

    if (_protocol == nullptr)
    {
        Debug_printf("FNJSON::parse() - NULL protocol.\r\n");
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

    Debug_printf("S: %s\r\n", _parseBuffer.c_str());
    _json = cJSON_Parse(_parseBuffer.c_str());

    if (_json == nullptr)
    {
        Debug_printf("FNJSON::parse() - Could not parse JSON\r\n");
        return false;
    }

    return true;
}

bool FNJSON::status(NetworkStatus *s)
{
    Debug_printf("FNJSON::status(%u) %s\r\n", json_bytes_remaining, getValue(_item).c_str());
    s->connected = true;
    s->rxBytesWaiting = json_bytes_remaining;
    s->error = json_bytes_remaining == 0 ? 136 : 0;
    return false;
}