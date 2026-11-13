#ifndef JSONPARSER_H
#define JSONPARSER_H

#include "QueryParser.h"
#include "fnjson.h" // FIXME - move all that in here

class JSONParser : public QueryParser
{
protected:
    FNJSON _json;

    void runQuery(const std::string &query, std::string &out) override;

public:
    JSONParser(NetworkProtocol *protocol) : QueryParser(protocol) {
        _json.setLineEnding(SYSTEM_BUS.nativeEOL());
        _json.setProtocol(_protocol);
    }

    error_is_true parse() override;
    error_is_true setQueryParam(uint8_t param) override;
    error_is_true setLineEnding(const std::string &eol) override;
    error_is_true setTextWidth(uint8_t width) override { _json.setTextWidth(width); RETURN_SUCCESS_AS_FALSE(); }

    parserMode_t mode() const override { return PARSER::JSON; }
    bool rawValue(std::string &out) override { return _json.rawValue(out); }
};

#endif /* JSONPARSER_H */
