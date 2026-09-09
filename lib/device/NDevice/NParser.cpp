#include "NParser.h"

fujiError_t NParser::read(std::string &buffer, size_t length)
{
    fujiError_t err = _protocol->read(length);
    if (err != FUJI_ERROR::NONE)
        return err;
    buffer.resize(length);
    std::copy(_protocol->receiveBuffer->begin(),
              _protocol->receiveBuffer->begin() + buffer.size(), buffer.begin());
    _protocol->receiveBuffer->erase(0, buffer.size());
    _protocol->receiveBuffer->shrink_to_fit();
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

    _protocol->status(&ns);
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
