#ifndef HTMLPARSER_H
#define HTMLPARSER_H

#include "NParser.h"
#include "fnhtml.h" // FIXME - move all that in here

class HTMLParser : public NParser
{
protected:
    FNHTML _html;

public:
    HTMLParser(NetworkProtocol *protocol) : NParser(protocol) {
        _html.setLineEnding(SYSTEM_BUS.nativeEOL());
        _html.setProtocol(_protocol);
    }

    fujiError_t write(std::string &buffer) override;
    off_t seek(off_t offset, int whence) override;

    error_is_true setQuery(const std::string &query) override;
    error_is_true parse() override;
    error_is_true setQueryParam(uint8_t param) override;
    error_is_true setLineEnding(const std::string &eol) override;
};

#endif /* HTMLPARSER_H */
