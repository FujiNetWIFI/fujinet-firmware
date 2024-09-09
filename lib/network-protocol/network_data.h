// network_data.h
#ifndef NETWORK_DATA_H
#define NETWORK_DATA_H

#include <cstdint>
#include <functional>
#include <memory>
#include <string>

class NetworkProtocol;
class FNJSON;
class PeoplesUrlParser;

struct NetworkData {
    std::unique_ptr<NetworkProtocol> protocol;
    std::unique_ptr<FNJSON> json;
    std::string receiveBuffer;
    std::string transmitBuffer;
    std::string specialBuffer;
    std::string deviceSpec;
    std::unique_ptr<PeoplesUrlParser> urlParser;
    std::string prefix;
    enum _channel_mode { PROTOCOL, JSON } channelMode = PROTOCOL;
    uint8_t translationMode = 0;
    std::string login;
    std::string password;
};

#endif // NETWORK_DATA_H