#ifndef NPARSER_H
#define NPARSER_H

#include "Protocol.h"

class NParser
{
protected:
    NetworkProtocol *_protocol = nullptr;

public:
    NParser(NetworkProtocol *protocol) : _protocol(protocol) {}
    virtual ~NParser() = default;

    fujiError_t read(std::string &buffer, size_t length);
    virtual fujiError_t write(std::string &buffer);
    size_t available();
    virtual off_t seek(off_t offset, int whence);

    virtual error_is_true setQuery(const std::string &query);
    virtual error_is_true parse();
    virtual error_is_true setQueryParam(uint8_t param);
    virtual error_is_true setLineEnding(const std::string &eol);

    NetworkStatus status();
};

#endif /* NPARSER_H */
