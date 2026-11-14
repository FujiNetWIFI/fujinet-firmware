#ifndef JSONPARSER_H
#define JSONPARSER_H

#include "NParser.h"
#include "fnjson.h" // FIXME - move all that in here

class JSONParser : public NParser
{
protected:
    FNJSON _json;

public:
    JSONParser(NetworkProtocol *protocol) : NParser(protocol) {
        _json.setLineEnding(SYSTEM_BUS.nativeEOL());
        _json.setProtocol(_protocol);
    }

    fujiError_t write(std::string &buffer) override;
    off_t seek(off_t offset, int whence) override;

    error_is_true setQuery(const std::string &query) override;
    error_is_true parse() override;
    error_is_true setQueryParam(uint8_t param) override;
    error_is_true setLineEnding(const std::string &eol) override;

    parserMode_t mode() const override { return PARSER::JSON; }
    std::optional<std::string> rawValue() override { return _json.rawValue(); }
    error_is_true setDocument(std::string document) override
    {
        _json.setDocument(std::move(document));
        RETURN_SUCCESS_AS_FALSE();
    }
};

#endif /* JSONPARSER_H */
