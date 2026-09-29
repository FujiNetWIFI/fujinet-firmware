#include "NParser.h"

fujiError_t NParser::read(std::string &buffer, size_t length)
{
    fujiError_t err = _protocol->read(length);
    if (err != FUJI_ERROR::NONE)
        return err;
    // The protocol can leave fewer bytes than asked for (translation folds CR/LF).
    buffer.assign(*_protocol->receiveBuffer, 0, std::min(length, _protocol->receiveBuffer->size()));
    _protocol->receiveBuffer->erase(0, buffer.size());
    return err;
}

fujiError_t NParser::write(std::string &buffer)
{
    fujiError_t err;

    std::string_view view(reinterpret_cast<const char*>(buffer.data()), buffer.size());
    *_protocol->transmitBuffer += view;
    return _protocol->write(view.size());
}

size_t NParser::available()
{
    // A parser set before OPEN has no protocol yet.
    if (_protocol == nullptr)
        return 0;
    return _protocol->available();
}

off_t NParser::seek(off_t offset, int whence)
{
    return _protocol->seek(offset, whence);
}

error_is_true NParser::setQuery(const std::string &query)
{
    // Nothing to parse, this is an error
    RETURN_ERROR_AS_TRUE();
}

error_is_true NParser::parse()
{
    // Nothing to parse, this is an error
    RETURN_ERROR_AS_TRUE();
}

NetworkStatus NParser::status()
{
    NetworkStatus ns;

    if (_protocol == nullptr)
    {
        ns.error = NDEV_STATUS::NOT_CONNECTED;
        return ns;
    }
    _protocol->status(&ns);
    if (_parseError != NDEV_STATUS::SUCCESS)
        ns.error = _parseError;
    return ns;
}

error_is_true NParser::setQueryParam(uint8_t param)
{
    RETURN_ERROR_AS_TRUE();
}

error_is_true NParser::setLineEnding(const std::string &eol)
{
    _protocol->setLineEnding(eol);
    RETURN_SUCCESS_AS_FALSE();
}
