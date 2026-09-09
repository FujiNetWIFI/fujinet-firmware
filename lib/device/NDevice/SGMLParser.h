#ifndef SGMLPARSER_H
#define SGMLPARSER_H

#include "NParser.h"
#include "fnsgml.h" // FIXME - move all that in here

class SGMLParser : public NParser
{
protected:
    FNSGML _sgml;

public:
    using NParser::NParser;
    SGMLParser(NetworkProtocol *protocol) : NParser(protocol) {
        _sgml.setLineEnding("\x0a");
        _sgml.setProtocol(_protocol);
    }

    fujiError_t write(std::string &buffer) override;
    off_t seek(off_t offset, int whence) override;

    error_is_true setQuery(const std::string &query) override;
    error_is_true parse() override;
    error_is_true setQueryParam(uint8_t param) override;
    error_is_true setLineEnding(const std::string &eol) override;
};

#endif /* SGMLPARSER_H */
