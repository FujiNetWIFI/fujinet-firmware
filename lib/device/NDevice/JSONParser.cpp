#include "JSONParser.h"

void JSONParser::runQuery(const std::string &query, std::string &buffer)
{
    _json.setReadQuery(query, _json.queryParam());
    buffer.resize(_json.available());
    _json.readValue(reinterpret_cast<uint8_t *>(buffer.data()), buffer.size());
    buffer.resize(strlen(buffer.c_str()));
    buffer = SYSTEM_BUS.unicodeTextToNative(buffer);
}

error_is_true JSONParser::parse()
{
    bool ok = _json.parse();

    _result.clear();

    _parseError = ok ? NDEV_STATUS::SUCCESS : NDEV_STATUS::COULD_NOT_PARSE_JSON;
    RETURN_ERROR_IF(!ok);
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
