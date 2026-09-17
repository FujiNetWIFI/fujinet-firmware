#ifndef XMLPARSER_H
#define XMLPARSER_H

#include "NParser.h"
#include "fnxml.h" // FIXME - move all that in here

class XMLParser : public NParser
{
protected:
    FNXML _xml;

public:
    using NParser::NParser;
    XMLParser(NetworkProtocol *protocol) : NParser(protocol) {
        _xml.setLineEnding("\x0a");
        _xml.setProtocol(_protocol);
    }

    fujiError_t write(std::string &buffer) override;
    off_t seek(off_t offset, int whence) override;

    error_is_true setQuery(const std::string &query) override;
    error_is_true parse() override;
    error_is_true setQueryParam(uint8_t param) override;
    error_is_true setLineEnding(const std::string &eol) override;
};

#endif /* XMLPARSER_H */
