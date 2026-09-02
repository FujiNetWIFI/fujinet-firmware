#include "JSONParser.h"

fujiError_t JSONParser::write(std::string &buffer)
{
    return FUJI_ERROR::UNSPECIFIED;
}

off_t JSONParser::seek(off_t offset, int whence)
{
    return -1;
}

error_is_true JSONParser::setQuery(const std::string &query)
{
    std::string buffer;

    _json.setReadQuery(query, 0);
    buffer.resize(_json.available());
    _json.readValue(reinterpret_cast<uint8_t *>(buffer.data()), buffer.size());
    buffer.resize(strlen(buffer.c_str()));
    buffer = SYSTEM_BUS.unicodeTextToNative(buffer);
    *_protocol->receiveBuffer += buffer;
    RETURN_SUCCESS_AS_FALSE();
}

error_is_true JSONParser::parse()
{
    _json.parse();
    RETURN_SUCCESS_AS_FALSE();
}

error_is_true JSONParser::setQueryParam(uint8_t param)
{
    _json.setQueryParam(param);
    RETURN_SUCCESS_AS_FALSE();
}

error_is_true JSONParser::setLineEnding(const std::string &eol)
{
    _json.setLineEnding(eol);
    RETURN_SUCCESS_AS_FALSE();
}
