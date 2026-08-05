/**
 * SGML/HTML/XML Wrapper for #FujiNet
 */

#include "fnsgml.h"

#include <string.h>

#include "Document.h"
#include "Selection.h"
#include "Node.h"

#include "../../include/debug.h"

namespace {

// Cap on the document we will buffer+parse; page size is attacker-controlled.
constexpr size_t kMaxBodyBytes = 512 * 1024;

// Per-read drain size. Must stay <= 65535 because NetworkProtocol::read() takes
// an unsigned short and larger values truncate (a multiple of 65536 -> 0).
constexpr size_t kReadChunkBytes = 32768;

} // namespace

FNSGML::FNSGML()
{
#ifdef VERBOSE_PROTOCOL
    Debug_printf("FNSGML::ctor()\r\n");
#endif
    _protocol = nullptr;
    _doc = nullptr;
}

FNSGML::~FNSGML()
{
#ifdef VERBOSE_PROTOCOL
    Debug_printf("FNSGML::dtor()\r\n");
#endif
    _protocol = nullptr;
    if (_doc != nullptr)
        delete _doc;
    _doc = nullptr;
}

void FNSGML::setLineEnding(const std::string &_lineEnding)
{
    lineEnding = _lineEnding;
}

void FNSGML::setProtocol(NetworkProtocol *newProtocol)
{
#ifdef VERBOSE_PROTOCOL
    Debug_printf("FNSGML::setProtocol()\r\n");
#endif
    _protocol = newProtocol;
}

void FNSGML::setQueryParam(uint8_t qp)
{
    _queryParam = qp;
}

/**
 * Set read query. queryString is a CSS selector (e.g. ".nav-link",
 * ".wp-block-table tbody tr:nth-child(2) td"), optionally followed by "@attr"
 * to return an attribute value instead of the element's text.
 *
 * Returns a single match. Calling again with the same selector advances to the
 * next match (so an 8-bit host iterates by repeating the query until it gets an
 * empty result); a different selector restarts at the first match.
 */
void FNSGML::setReadQuery(const std::string &queryString, uint8_t queryParam)
{
#ifdef VERBOSE_PROTOCOL
    Debug_printf("FNSGML::setReadQuery queryString: %s, queryParam: %d\r\n", queryString.c_str(), queryParam);
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
    _sgml_bytes_remaining = readValueLen();
}

/**
 * Run the CSS selector against the parsed document and build the result value.
 */
void FNSGML::resolveQuery()
{
    _value.clear();

    if (_doc == nullptr)
        return;

    // Split an optional "@attr" suffix off the selector.
    std::string selector = _queryString;
    std::string attr;
    size_t at = _queryString.rfind('@');
    if (at != std::string::npos)
    {
        selector = _queryString.substr(0, at);
        attr = _queryString.substr(at + 1);
        while (!selector.empty() && selector.back() == ' ')
            selector.pop_back();
    }
    if (selector.empty())
        return;

    // gumbo-query is patched to be exception-free: a malformed selector yields
    // an empty selection rather than throwing, so no try/catch is needed (the
    // ESP firmware builds with C++ exceptions disabled).
    CSelection sel = _doc->find(selector);

    // Return only the current match; an out-of-range index (iteration finished)
    // leaves _value empty so the caller sees 0 bytes available.
    if (_matchIndex < sel.nodeNum())
    {
        CNode node = sel.nodeAt(_matchIndex);
        std::string v = attr.empty() ? node.text() : node.attribute(attr);
        _value = processString(v) + lineEnding;
    }
}

/**
 * Character remapping for target platforms (mirrors FNJSON).
 */
std::string FNSGML::processString(std::string in)
{
#ifdef BUILD_ATARI
    if (_queryParam & SGML_REMAP_CHARS)
    {
        if (_queryParam & SGML_REMAP_ATASCII_INTERNATIONAL)
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

bool FNSGML::readValue(uint8_t *rx_buf, unsigned short len)
{
    if (_value.empty())
        return true; // error

    memcpy(rx_buf, _value.data(), len);
    _sgml_bytes_remaining -= len;

    return false; // no error
}

int FNSGML::readValueLen()
{
    return _value.size();
}

/**
 * Parse document from protocol
 */
bool FNSGML::parse()
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
                Debug_printf("FNSGML::parse() - body exceeds %zu bytes, truncating\r\n", kMaxBodyBytes);
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

    // Gumbo does full HTML5 error-recovery, so the raw (malformed) body is fine.
    CDocument *doc = new CDocument();
    doc->parse(_parseBuffer);
    _doc = doc;

    // Fresh document -> restart match iteration.
    _lastQuery.clear();
    _matchIndex = 0;
    return true;
}

bool FNSGML::status(NetworkStatus *s)
{
    s->connected = true;
    s->error = _sgml_bytes_remaining == 0 ? NDEV_STATUS::END_OF_FILE : NDEV_STATUS::SUCCESS;
    return false;
}
