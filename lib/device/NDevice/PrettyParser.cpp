#include "PrettyParser.h"

#include "bus.h"

#include <errno.h>
#include <stdlib.h>

PrettyParser::PrettyParser(NetworkProtocol *protocol) : NParser(protocol)
{
    applyDefaults();
}

PrettyParser::PrettyParser(NetworkProtocol *protocol, std::unique_ptr<NParser> saved, std::string value)
    : NParser(protocol), _saved(std::move(saved)), _value(std::move(value)), _valueMode(true)
{
    applyDefaults();
}

void PrettyParser::applyDefaults()
{
    _eol = SYSTEM_BUS.nativeEOL();
    if (_protocol != nullptr && !_protocol->native_eol.empty())
        _eol = _protocol->native_eol; // includes a NET_SET_EOL override
    _pretty.setLineEnding(_eol);
    _pretty.setProtocol(_protocol);

    _pretty.setNativeCharset(busNativeCharset());
}

fujiError_t PrettyParser::write(std::string &)
{
    return FUJI_ERROR::UNSPECIFIED;
}

off_t PrettyParser::seek(off_t, int)
{
    return -1;
}

size_t PrettyParser::available()
{
    return _linkPending ? _staged.size() : _pretty.available();
}

fujiError_t PrettyParser::read(std::string &buffer, size_t length)
{
    if (_linkPending)
    {
        // Serving a staged link URL: never mix page text into it. The read
        // that drains it hands the page back.
        buffer.assign(_staged, 0, length);
        _staged.erase(0, buffer.size());
        if (_staged.empty())
            _linkPending = false;
        return buffer.empty() ? FUJI_ERROR::UNSPECIFIED : FUJI_ERROR::NONE;
    }

    size_t n = std::min(length, _pretty.available());
    if (n == 0)
        return FUJI_ERROR::UNSPECIFIED; // nothing left: tell the client this read failed

    buffer.resize(n);
    n = _pretty.readValue(reinterpret_cast<uint8_t *>(buffer.data()), n);
    buffer.resize(n);
    return FUJI_ERROR::NONE;
}

error_is_true PrettyParser::parse()
{
    _staged.clear();
    _linkPending = false; // a new page invalidates any staged link URL

    if (_protocol != nullptr)
    {
        if (_protocol->opened_url != nullptr)
            _pretty.setBaseUrl(_protocol->opened_url->mRawUrl);
        if (!_eolExplicit && !_protocol->native_eol.empty())
        {
            _eol = _protocol->native_eol;
            _pretty.setLineEnding(_eol);
        }
    }

    bool ok;
    if (_valueMode)
    {
        ok = _pretty.renderDocument(_value);
        _value.clear(); // the parsed document is retained
        _value.shrink_to_fit();
    }
    else
        ok = _pretty.parse();

    if (!ok)
        _parseError = NDEV_STATUS::GENERAL;
    else if (_pretty.available() == 0)
        _parseError = NDEV_STATUS::END_OF_FILE; // fetched, but nothing rendered
    else
        _parseError = NDEV_STATUS::SUCCESS;

    RETURN_ERROR_IF(_parseError != NDEV_STATUS::SUCCESS);
}

/**
 * Split "<selector>@<attr>". The split is at the last '@' that is outside
 * quotes and outside [...] brackets, so a[href="mailto:x@y"] stays a plain
 * selector. The attribute must be a non-empty run of [A-Za-z0-9_:-] and the
 * selector non-empty.
 */
static bool splitAttrQuery(const std::string &query, std::string &selector, std::string &attr)
{
    size_t at = std::string::npos;
    int depth = 0;
    char quote = 0;

    for (size_t i = 0; i < query.size(); i++)
    {
        char c = query[i];
        if (quote != 0)
        {
            if (c == '\\')
                i++;
            else if (c == quote)
                quote = 0;
        }
        else if (c == '"' || c == '\'')
            quote = c;
        else if (c == '[')
            depth++;
        else if (c == ']' && depth > 0)
            depth--;
        else if (c == '@' && depth == 0)
            at = i;
    }

    if (at == std::string::npos || at == 0 || at + 1 >= query.size())
        return false;

    for (size_t i = at + 1; i < query.size(); i++)
    {
        char c = query[i];
        if (!((c >= 'A' && c <= 'Z') || (c >= 'a' && c <= 'z') || (c >= '0' && c <= '9') ||
              c == '_' || c == ':' || c == '-'))
            return false;
    }

    selector = query.substr(0, at);
    attr = query.substr(at + 1);
    return true;
}

error_is_true PrettyParser::setQuery(const std::string &query)
{
    // An all-digits query is a link index, never a CSS selector (a CSS
    // identifier cannot start with a digit).
    bool isIndex = !query.empty();
    for (char c : query)
    {
        if (c < '0' || c > '9')
        {
            isIndex = false;
            break;
        }
    }

    if (isIndex)
    {
        errno = 0;
        unsigned long parsed = strtoul(query.c_str(), nullptr, 10);
        size_t index = (errno == ERANGE) ? 0 : (size_t) parsed; // overflow: no such link
        std::string url = _pretty.linkUrl(index);

        if (url.empty()) // no such link, index 0, or links never collected
        {
            _staged.clear();
            _linkPending = false;
            _parseError = NDEV_STATUS::END_OF_FILE;
            RETURN_ERROR_AS_TRUE();
        }

        _staged = url + _eol;
        _linkPending = true;
        _parseError = NDEV_STATUS::SUCCESS;
        RETURN_SUCCESS_AS_FALSE();
    }

    // "<selector>@<attr>": the attribute of the first match, staged exactly
    // like a link URL. The page and its section are left untouched.
    std::string attrSelector, attrName;
    if (query[0] != '!' && splitAttrQuery(query, attrSelector, attrName))
    {
        std::string value = _pretty.attrOfFirst(attrSelector, attrName);
        if (value.empty()) // no match, or the attribute is missing/empty
        {
            _staged.clear();
            _linkPending = false;
            _parseError = NDEV_STATUS::END_OF_FILE;
            RETURN_ERROR_AS_TRUE();
        }

        _staged = value + _eol;
        _linkPending = true;
        _parseError = NDEV_STATUS::SUCCESS;
        RETURN_SUCCESS_AS_FALSE();
    }

    _pretty.setQuery(query);

    // A re-render replaces the previous section, so drop anything staged from it.
    _staged.clear();
    _linkPending = false;
    _pretty.rerender(); // false just means nothing has been parsed yet
    if (_pretty.available() > 0)
        _parseError = NDEV_STATUS::SUCCESS;
    RETURN_SUCCESS_AS_FALSE();
}

error_is_true PrettyParser::setQueryParam(uint8_t)
{
    // The remap/output flags mean nothing to a renderer; accept and ignore so
    // a client that sends them anyway is not refused.
    RETURN_SUCCESS_AS_FALSE();
}

error_is_true PrettyParser::setLineEnding(const std::string &eol)
{
    _eol = eol;
    _eolExplicit = true;
    _pretty.setLineEnding(eol);
    _staged.clear();
    _linkPending = false;
    _pretty.rerender();
    RETURN_SUCCESS_AS_FALSE();
}

error_is_true PrettyParser::setTextWidth(uint8_t width)
{
    _pretty.setScreenWidth(width);
    _staged.clear();
    _linkPending = false;
    _pretty.rerender();
    RETURN_SUCCESS_AS_FALSE();
}

error_is_true PrettyParser::setRenderLinks(bool on)
{
    _pretty.setRenderLinks(on);
    _staged.clear();
    _linkPending = false;
    _pretty.rerender();
    RETURN_SUCCESS_AS_FALSE();
}

error_is_true PrettyParser::setNativeGraphics(bool on)
{
    _pretty.setNativeGraphics(on);
    _staged.clear();
    _linkPending = false;
    _pretty.rerender();
    RETURN_SUCCESS_AS_FALSE();
}

NetworkStatus PrettyParser::status()
{
    NetworkStatus ns;
    size_t remaining = available();

    ns.connected = remaining > 0;
    ns.error = remaining > 0 ? NDEV_STATUS::SUCCESS : NDEV_STATUS::END_OF_FILE;
    if (_parseError != NDEV_STATUS::SUCCESS && remaining == 0)
        ns.error = _parseError;
    return ns;
}
