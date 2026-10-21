#ifndef ADAMNETWORKRECEIVE_STUBS_H
#define ADAMNETWORKRECEIVE_STUBS_H

#include <string>

// Transaction counts and reply captured by the systemBus stub.
struct BusLog
{
    int success = 0;
    int error = 0;
    std::string sent;
};

extern BusLog bus;

#endif // ADAMNETWORKRECEIVE_STUBS_H
