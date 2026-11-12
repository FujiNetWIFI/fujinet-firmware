#ifndef HTMLPARSER_H
#define HTMLPARSER_H

#include "QueryParser.h"
#include "fnhtml.h" // FIXME - move all that in here

class HTMLParser : public QueryParser
{
protected:
    FNHTML _html;

    void runQuery(const std::string &query, std::string &out) override;

public:
    HTMLParser(NetworkProtocol *protocol) : QueryParser(protocol) {
        _html.setLineEnding(SYSTEM_BUS.nativeEOL());
        _html.setProtocol(_protocol);
    }

    error_is_true parse() override;
    error_is_true setQueryParam(uint8_t param) override;
    error_is_true setLineEnding(const std::string &eol) override;
    error_is_true setTextWidth(uint8_t width) override { _html.setTextWidth(width); RETURN_SUCCESS_AS_FALSE(); }

    parserMode_t mode() const override { return PARSER::HTML; }
    bool rawValue(std::string &out) override { return _html.rawValue(out); }
};

#endif /* HTMLPARSER_H */
