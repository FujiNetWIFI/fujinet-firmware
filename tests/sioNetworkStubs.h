#ifndef SIONETWORK_STUBS_H
#define SIONETWORK_STUBS_H

#include <string>

// What the systemBus stubs in sioNetworkStubs.cpp were told to answer.
struct BusLog
{
    int success = 0;
    int error = 0;
    std::string sent;
    bool sent_error = false;
};

extern BusLog bus;

#endif // SIONETWORK_STUBS_H
