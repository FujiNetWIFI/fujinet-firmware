/**
 * XML Wrapper for #FujiNet
 */

#include "fnxml.h"

#include <string.h>
#include <vector>
#include <cstdlib>
#include <cctype>

#include "tinyxml2.h"

#include "fnxml_query.h"

#include "../../include/debug.h"

namespace {

// Cap on the document we will buffer+parse; page size is attacker-controlled.
constexpr size_t kMaxBodyBytes = 512 * 1024;

// Per-read drain size. Must stay <= 65535 because NetworkProtocol::read() takes
// an unsigned short and larger values truncate (a multiple of 65536 -> 0).
constexpr size_t kReadChunkBytes = 32768;

} // namespace

FNXML::FNXML()
{
#ifdef VERBOSE_PROTOCOL
    Debug_printf("FNXML::ctor()\r\n");
#endif
    _protocol = nullptr;
    _doc = nullptr;
}

FNXML::~FNXML()
{
#ifdef VERBOSE_PROTOCOL
    Debug_printf("FNXML::dtor()\r\n");
#endif
    _protocol = nullptr;
    if (_doc != nullptr)
        delete _doc;
    _doc = nullptr;
}

void FNXML::setLineEnding(const std::string &_lineEnding)
{
    lineEnding = _lineEnding;
}

void FNXML::setProtocol(NetworkProtocol *newProtocol)
{
#ifdef VERBOSE_PROTOCOL
    Debug_printf("FNXML::setProtocol()\r\n");
#endif
    _protocol = newProtocol;
}

void FNXML::setQueryParam(uint8_t qp)
{
    _queryParam = qp;
}

/**
 * Set read query. queryString is an XPath-subset query (e.g. "/rss/channel/item",
 * "//title", "item[@id='x']"), optionally followed by "@attr" to return an attribute
 * value instead of the element's text.
 *
 * Returns a single match. Calling again with the same selector advances to the
 * next match (so an 8-bit host iterates by repeating the query until it gets an
 * empty result); a different selector restarts at the first match.
 */
void FNXML::setReadQuery(const std::string &queryString, uint8_t queryParam)
{
#ifdef VERBOSE_PROTOCOL
    Debug_printf("FNXML::setReadQuery queryString: %s, queryParam: %d\r\n", queryString.c_str(), queryParam);
#endif
    if (queryString == _lastQuery)
    {
        _matchIndex++;
    }
    else
    {
        _lastQuery = queryString;
        _matchIndex = 0;
    }
    _queryString = queryString;
    _queryParam = queryParam;
    resolveQuery();
    _xml_bytes_remaining = readValueLen();
}

/**
 * Run the query (see fnxml_query.h for the supported XPath subset) and build the
 * result value for the current match index.
 */
void FNXML::resolveQuery()
{
    _value.clear();

    std::string v;
    if (fnxml_resolve_query(_doc, _queryString, _matchIndex, v))
        _value = processString(v) + lineEnding;
}

/**
 * Character remapping for target platforms (mirrors FNHTML).
 */
std::string FNXML::processString(std::string in)
{
#ifdef BUILD_ATARI
    if (_queryParam & XML_REMAP_CHARS)
    {
        if (_queryParam & XML_REMAP_ATASCII_INTERNATIONAL)
        {
            std::string mapFrom[] = {"á", "ù", "Ñ", "É", "ç", "ô", "ò", "ì", "£", "ï", "ü", "ä", "Ö", "ú", "ó", "ö", "Ü", "â", "û", "î", "é", "è", "ñ", "ê", "å", "à", "Å", "¡", "Ä", "ß"};
            std::string mapTo[] = {"\x00", "\x01", "\x02", "\x03", "\x04", "\x05", "\x06", "\x07", "\x08", "\x09", "\x0a", "\x0b", "\x0c", "\x0d", "\x0e", "\x0f", "\x10", "\x11", "\x12", "\x13", "\x14", "\x15", "\x16", "\x17", "\x18", "\x19", "\x1a", "\x60", "\x7b", "ss"};
            int elementCount = sizeof(mapFrom) / sizeof(mapFrom[0]);
            for (int elementIndex = 0; elementIndex < elementCount; elementIndex++)
                if (in.find(mapFrom[elementIndex]) != std::string::npos)
                    in.replace(in.find(mapFrom[elementIndex]), std::string(mapFrom[elementIndex]).size(), mapTo[elementIndex]);
        }
        else
        {
            std::string mapFrom[] = {"Ä", "Ö", "Ü", "ä", "ö", "ü", "ß", "é", "è", "á", "à", "ó", "ò", "ú", "ù"};
            std::string mapTo[] = {"Ae", "Oe", "Ue", "ae", "oe", "ue", "ss", "e", "e", "a", "a", "o", "o", "u", "u"};
            int elementCount = sizeof(mapFrom) / sizeof(mapFrom[0]);
            for (int elementIndex = 0; elementIndex < elementCount; elementIndex++)
                if (in.find(mapFrom[elementIndex]) != std::string::npos)
                    in.replace(in.find(mapFrom[elementIndex]), std::string(mapFrom[elementIndex]).size(), mapTo[elementIndex]);
        }
    }
#endif
    return in;
}

bool FNXML::readValue(uint8_t *rx_buf, unsigned short len)
{
    if (_value.empty())
        return true; // error

    memcpy(rx_buf, _value.data(), len);
    _xml_bytes_remaining -= len;

    return false; // no error
}

int FNXML::readValueLen()
{
    return _value.size();
}

/**
 * Parse document from protocol
 */
bool FNXML::parse()
{
    NetworkStatus ns;

    if (_doc != nullptr)
    {
        delete _doc;
        _doc = nullptr;
    }

    if (_protocol == nullptr)
        return false;

    _parseBuffer.clear();
    _protocol->status(&ns);
    while (ns.connected || _protocol->available() > 0)
    {
        size_t avail = _protocol->available();
        if (avail > 0)
        {
            // NetworkProtocol::read() takes unsigned short, so a raw available()
            // >= 65536 truncates (e.g. 65536 -> 0) and stalls the drain forever.
            // Pull in bounded chunks that can never truncate to zero.
            unsigned short chunk = avail > kReadChunkBytes ? (unsigned short)kReadChunkBytes
                                                           : (unsigned short)avail;
            _protocol->read(chunk);
            if (_protocol->receiveBuffer->empty())
                break; // no forward progress (EOF/stalled) - stop draining
            _parseBuffer += *_protocol->receiveBuffer;
            _protocol->receiveBuffer->clear();
            if (_parseBuffer.size() > kMaxBodyBytes)
            {
#ifdef VERBOSE_PROTOCOL
                Debug_printf("FNXML::parse() - body exceeds %zu bytes, truncating\r\n", kMaxBodyBytes);
#endif
                _parseBuffer.resize(kMaxBodyBytes);
                break;
            }
        }
        _protocol->status(&ns);
#ifdef ESP_PLATFORM
        vTaskDelay(10);
#endif
    }

    if (_parseBuffer.empty())
        return false;

    // Strip UTF-8 BOM if present.
    if (_parseBuffer.length() >= 3 &&
        (unsigned char)_parseBuffer[0] == 0xEF &&
        (unsigned char)_parseBuffer[1] == 0xBB &&
        (unsigned char)_parseBuffer[2] == 0xBF)
    {
        _parseBuffer = _parseBuffer.substr(3);
    }

    // Strip leading whitespace (tinyxml2 is strict about declaration position).
    size_t firstNonWs = 0;
    while (firstNonWs < _parseBuffer.length() && isspace((unsigned char)_parseBuffer[firstNonWs]))
        firstNonWs++;
    if (firstNonWs > 0 && firstNonWs < _parseBuffer.length())
        _parseBuffer = _parseBuffer.substr(firstNonWs);

    if (_parseBuffer.empty())
        return false;

    // Parse with tinyxml2.
    _doc = new tinyxml2::XMLDocument();
    tinyxml2::XMLError err = _doc->Parse(_parseBuffer.c_str(), _parseBuffer.size());

    if (err != tinyxml2::XML_SUCCESS)
    {
#ifdef VERBOSE_PROTOCOL
        Debug_printf("FNXML::parse() - XML parse error: %s\r\n", _doc->ErrorStr());
#endif
        delete _doc;
        _doc = nullptr;
        return false;
    }

    // Fresh document -> restart match iteration.
    _lastQuery.clear();
    _matchIndex = 0;
    return true;
}

bool FNXML::status(NetworkStatus *s)
{
    s->connected = true;
    s->error = _xml_bytes_remaining == 0 ? NDEV_STATUS::END_OF_FILE : NDEV_STATUS::SUCCESS;
    return false;
}
