#ifndef QUERYPARSER_H
#define QUERYPARSER_H

#include "NParser.h"

#include <algorithm>
#include <stdint.h>

/**
 * Shared behaviour of the XML, JSON and HTML parsers: each query replaces the
 * unread data in the receive buffer with its result, and a copy of that
 * result is kept so the client can seek within it. Derived classes supply
 * the document-specific query and raw-value lookup.
 */
class QueryParser : public NParser
{
protected:
    // The current result as served (native charset), whole.
    std::string _result;

    // Run the query; out is the result as served.
    virtual void runQuery(const std::string &query, std::string &out) = 0;

public:
    QueryParser(NetworkProtocol *protocol) : NParser(protocol) {}

    fujiError_t write(std::string &) override { return FUJI_ERROR::UNSPECIFIED; }

    error_is_true setQuery(const std::string &query) override
    {
        // A parser set before OPEN has no protocol yet.
        if (_protocol == nullptr)
            RETURN_ERROR_AS_TRUE();
        runQuery(query, _result);
        *_protocol->receiveBuffer = _result;
        RETURN_SUCCESS_AS_FALSE();
    }

    // SEEK_SET: unread data becomes _result[offset..], offset clamped to the length.
    // SEEK_CUR with 0: bytes of the result already read. Anything else: -1.
    off_t seek(off_t offset, int whence) override
    {
        if (_protocol == nullptr)
            return -1;
        std::string &rx = *_protocol->receiveBuffer;

        if (whence == SEEK_CUR && offset == 0)
            return (off_t) (_result.size() - std::min(_result.size(), rx.size()));
        if (whence != SEEK_SET)
            return -1;

        // The bus sends a uint32; where off_t is 32 bits 0xFFFFFFFF arrives as -1.
        size_t pos = std::min(_result.size(), (size_t) (uint32_t) offset);
        rx.assign(_result, pos, std::string::npos);
        return (off_t) pos;
    }
};

#endif /* QUERYPARSER_H */
