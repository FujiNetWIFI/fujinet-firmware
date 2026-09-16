#include "HTMLParser.h"

fujiError_t HTMLParser::write(std::string &buffer)
{
    return FUJI_ERROR::UNSPECIFIED;
}

off_t HTMLParser::seek(off_t offset, int whence)
{
    return -1;
}

error_is_true HTMLParser::setQuery(const std::string &query)
{
    std::string buffer;

    _html.setReadQuery(query, _html.queryParam());
    buffer.resize(_html.available());
    _html.readValue(reinterpret_cast<uint8_t *>(buffer.data()), buffer.size());
    buffer.resize(strlen(buffer.c_str()));
    buffer = SYSTEM_BUS.unicodeTextToNative(buffer);
    *_protocol->receiveBuffer += buffer;
    RETURN_SUCCESS_AS_FALSE();
}

error_is_true HTMLParser::parse()
{
    bool ok = _html.parse();

    _parseError = ok ? NDEV_STATUS::SUCCESS : NDEV_STATUS::GENERAL;
    RETURN_ERROR_IF(!ok);
}

error_is_true HTMLParser::setQueryParam(uint8_t param)
{
    _html.setQueryParam(param);
    RETURN_SUCCESS_AS_FALSE();
}

error_is_true HTMLParser::setLineEnding(const std::string &eol)
{
    _html.setLineEnding(eol);
    RETURN_SUCCESS_AS_FALSE();
}
