// network_data.h
#ifndef NETWORK_DATA_H
#define NETWORK_DATA_H

#include <cstdint>
#include <functional>
#include <memory>
#include <string>

#include "status_error_codes.h"

class NetworkProtocol;
class FNJSON;
class PeoplesUrlParser;

typedef enum class CHANNEL_MODE {
    PROTOCOL = 0,
    JSON     = 1,
} channelMode_t;

struct NetworkData {
    std::unique_ptr<NetworkProtocol> protocol;
    std::unique_ptr<FNJSON> json;
    std::string receiveBuffer;
    std::string transmitBuffer;
    std::string specialBuffer;
    std::string deviceSpec;
    std::unique_ptr<PeoplesUrlParser> urlParser;
    std::string prefix;
    channelMode_t channelMode = CHANNEL_MODE::PROTOCOL;
    uint8_t translationMode = 0;
    std::string login;
    std::string password;
    // Result of the last OPEN on this channel.  A failed protocol open
    // destroys the protocol instance, so the specific error (e.g.
    // FILE_NOT_FOUND) must be remembered here for a later STATUS to report.
    nDevStatus_t open_error = NDEV_STATUS::SUCCESS;
};

#endif // NETWORK_DATA_H
