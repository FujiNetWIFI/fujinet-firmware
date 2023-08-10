#ifndef PROTOCOLPARSER_H
#define PROTOCOLPARSER_H

#include <string>
#include "Protocol.h"

class ProtocolParser
{
public:
    ProtocolParser();
    ~ProtocolParser();
    NetworkProtocol* createProtocol(std::string scheme, std::string *receiveBuffer, std::string *transmitBuffer, std::string *specialBuffer, std::string *login, std::string *password);
};

#endif /* PROTOCOLPARSER_H */