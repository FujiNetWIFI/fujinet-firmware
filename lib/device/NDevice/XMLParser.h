#ifndef XMLPARSER_H
#define XMLPARSER_H

#include "QueryParser.h"
#include "fnxml.h" // FIXME - move all that in here

class XMLParser : public QueryParser
{
protected:
    FNXML _xml;

    void runQuery(const std::string &query, std::string &out) override;

public:
    XMLParser(NetworkProtocol *protocol) : QueryParser(protocol) {
        _xml.setLineEnding(SYSTEM_BUS.nativeEOL());
        _xml.setProtocol(_protocol);
    }

    error_is_true parse() override;
    error_is_true setQueryParam(uint8_t param) override;
    error_is_true setLineEnding(const std::string &eol) override;
    error_is_true setTextWidth(uint8_t width) override { _xml.setTextWidth(width); RETURN_SUCCESS_AS_FALSE(); }

    parserMode_t mode() const override { return PARSER::XML; }
    bool rawValue(std::string &out) override { return _xml.rawValue(out); }
};

#endif /* XMLPARSER_H */
