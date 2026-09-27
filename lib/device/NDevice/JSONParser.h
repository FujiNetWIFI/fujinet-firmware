#ifndef JSONPARSER_H
#define JSONPARSER_H

#include "NParser.h"
#include "fnjson.h" // FIXME - move all that in here

class JSONParser : public NParser
{
protected:
    FNJSON _json;

public:
    using NParser::NParser;
    JSONParser(NetworkProtocol *protocol) : NParser(protocol) {
        _json.setLineEnding("\x0a");
        _json.setProtocol(_protocol);
    }

    fujiError_t write(std::string &buffer) override;
    off_t seek(off_t offset, int whence) override;

    error_is_true setQuery(const std::string &query) override;
    error_is_true parse() override;
    error_is_true setQueryParam(uint8_t param) override;
    error_is_true setLineEnding(const std::string &eol) override;
};

#endif /* JSONPARSER_H */
