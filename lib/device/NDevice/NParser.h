#ifndef NPARSER_H
#define NPARSER_H

#include "Protocol.h"

#include <memory>

typedef enum class PARSER : uint8_t {
    NONE = 0,
    JSON = 1,
    HTML = 2, // HTML via CSS selector (FNHTML, Gumbo)
    XML  = 3, // XML via an XPath subset (FNXML, tinyxml2)
    PRETTY = 4, // HTML rendered to word-wrapped plain text (FNPretty, Gumbo)
} parserMode_t;

class NParser
{
protected:
    NetworkProtocol *_protocol = nullptr;

    // Last parse() result, so STATUS can say why the channel is empty. Set by each parse().
    nDevStatus_t _parseError = NDEV_STATUS::SUCCESS;

public:
    NParser(NetworkProtocol *protocol) : _protocol(protocol) {}
    virtual ~NParser() = default;

    virtual fujiError_t read(std::string &buffer, size_t length);
    virtual fujiError_t write(std::string &buffer);
    virtual size_t available();
    virtual off_t seek(off_t offset, int whence);

    virtual error_is_true setQuery(const std::string &query);
    virtual error_is_true parse();
    virtual error_is_true setQueryParam(uint8_t param);
    virtual error_is_true setLineEnding(const std::string &eol);
    // Text width in columns (NET_SET_PARAMETER param 2); 0 = parser default.
    virtual error_is_true setTextWidth(uint8_t width);
    // PRETTY only (NET_SET_PARAMETER params 4 and 3); others reject them.
    virtual error_is_true setRenderLinks(bool on);
    virtual error_is_true setNativeGraphics(bool on);

    virtual NetworkStatus status();

    // Which parser this is, so NET_SET_PARSER can tell whether a mode is already in place.
    virtual parserMode_t mode() const { return PARSER::NONE; }

    // The last query's value as it is in the document: no query flags applied, no charset
    // conversion. False when there has been no query. Query parsers only.
    virtual bool rawValue(std::string &out) { return false; }

    // PRETTY over a query parser's value keeps that parser here; others hold none.
    virtual NParser *savedParser() { return nullptr; }
    virtual std::unique_ptr<NParser> takeSavedParser() { return nullptr; }
};

// Bound after a failed OPEN in place of the protocol, so STATUS can report why.
class NErrorParser : public NParser
{
    nDevStatus_t _error;

public:
    NErrorParser(nDevStatus_t error) : NParser(nullptr), _error(error) {}

    fujiError_t read(std::string &, size_t) override { return FUJI_ERROR::UNSPECIFIED; }
    fujiError_t write(std::string &) override { return FUJI_ERROR::UNSPECIFIED; }
    size_t available() override { return 0; }
    off_t seek(off_t, int) override { return -1; }
    error_is_true setLineEnding(const std::string &) override { RETURN_ERROR_AS_TRUE(); }

    NetworkStatus status() override
    {
        NetworkStatus ns;
        ns.error = _error;
        return ns;
    }
};

#endif /* NPARSER_H */
