#ifndef NETWORK_PROTOCOL_FACTORY_H
#define NETWORK_PROTOCOL_FACTORY_H

#include <algorithm>
#include <cctype>
#include <functional>
#include <memory>
#include <string>
#include <unordered_map>

#include "network_data.h"

#include "ProtocolParser.h"

class NetworkProtocolFactory
{
public:
    static std::unique_ptr<NetworkProtocol> createProtocol(const std::string &scheme, NetworkData &data)
    {
        return ProtocolParser::createProtocol(scheme, &data.receiveBuffer,
                                              &data.transmitBuffer,
                                              &data.specialBuffer,
                                              &data.login, &data.password);
    }
};

#endif // NETWORK_PROTOCOL_FACTORY_H
