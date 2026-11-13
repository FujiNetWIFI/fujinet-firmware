#include "XMLParser.h"

void XMLParser::runQuery(const std::string &query, std::string &buffer)
{
    _xml.setReadQuery(query, _xml.queryParam());
    buffer.resize(_xml.available());
    _xml.readValue(reinterpret_cast<uint8_t *>(buffer.data()), buffer.size());
    buffer.resize(strlen(buffer.c_str()));
    buffer = SYSTEM_BUS.unicodeTextToNative(buffer);
}

error_is_true XMLParser::parse()
{
    bool ok = _xml.parse();

    _result.clear();

    _parseError = ok ? NDEV_STATUS::SUCCESS : NDEV_STATUS::GENERAL;
    RETURN_ERROR_IF(!ok);
}

error_is_true XMLParser::setQueryParam(uint8_t param)
{
    _xml.setQueryParam(param);
    RETURN_SUCCESS_AS_FALSE();
}

error_is_true XMLParser::setLineEnding(const std::string &eol)
{
    _xml.setLineEnding(eol);
    RETURN_SUCCESS_AS_FALSE();
}
