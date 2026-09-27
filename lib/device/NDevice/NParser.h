#ifndef NPARSER_H
#define NPARSER_H

#include "Protocol.h"

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

    virtual NetworkStatus status();
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
