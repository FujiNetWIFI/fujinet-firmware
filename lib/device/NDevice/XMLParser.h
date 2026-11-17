#ifndef XMLPARSER_H
#define XMLPARSER_H

#include "NParser.h"
#include "fnxml.h" // FIXME - move all that in here

class XMLParser : public NParser
{
protected:
    FNXML _xml;

public:
    XMLParser(NetworkProtocol *protocol) : NParser(protocol) {
        _xml.setLineEnding(SYSTEM_BUS.nativeEOL());
        _xml.setProtocol(_protocol);
    }

    fujiError_t write(std::string &buffer) override;
    off_t seek(off_t offset, int whence) override;

    error_is_true setQuery(const std::string &query) override;
    error_is_true parse() override;
    error_is_true setQueryParam(uint8_t param) override;
    error_is_true setLineEnding(const std::string &eol) override;

    parserMode_t mode() const override { return PARSER::XML; }
    std::optional<std::string> rawValue() override { return _xml.rawValue(); }
    error_is_true setDocument(std::string document) override
    {
        _xml.setDocument(std::move(document));
        RETURN_SUCCESS_AS_FALSE();
    }
};

#endif /* XMLPARSER_H */
