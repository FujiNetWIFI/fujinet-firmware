#include "SGMLParser.h"

fujiError_t SGMLParser::write(std::string &buffer)
{
    return FUJI_ERROR::UNSPECIFIED;
}

off_t SGMLParser::seek(off_t offset, int whence)
{
    return -1;
}

error_is_true SGMLParser::setQuery(const std::string &query)
{
    std::string buffer;

    _sgml.setReadQuery(query, 0);
    buffer.resize(_sgml.available());
    _sgml.readValue(reinterpret_cast<uint8_t *>(buffer.data()), buffer.size());
    buffer.resize(strlen(buffer.c_str()));
    buffer = SYSTEM_BUS.unicodeTextToNative(buffer);
    *_protocol->receiveBuffer += buffer;
    RETURN_SUCCESS_AS_FALSE();
}

error_is_true SGMLParser::parse()
{
    _sgml.parse();
    RETURN_SUCCESS_AS_FALSE();
}

error_is_true SGMLParser::setQueryParam(uint8_t param)
{
    _sgml.setQueryParam(param);
    RETURN_SUCCESS_AS_FALSE();
}

error_is_true SGMLParser::setLineEnding(const std::string &eol)
{
    _sgml.setLineEnding(eol);
    RETURN_SUCCESS_AS_FALSE();
}
