#include "ProtocolParser.h"

#include <algorithm>

#include "TCP.h"
#include "UDP.h"
#include "Test.h"
#include "Telnet.h"
#include "TNFS.h"
#include "FTP.h"
#include "HTTP.h"
#include "SSH.h"
#include "SMB.h"

#include "../utils/string_utils.h"
#include "../../include/debug.h"

ProtocolParser::ProtocolParser() {}
ProtocolParser::~ProtocolParser() {}

NetworkProtocol* ProtocolParser::createProtocol(std::string scheme, std::string *receiveBuffer, std::string *transmitBuffer, std::string *specialBuffer, std::string *login, std::string *password)
{
    NetworkProtocol* protocol = nullptr;

    std::transform(scheme.begin(), scheme.end(), scheme.begin(), ::toupper);

    switch (hash_djb2a(scheme))
    {
        case "TCP"_sh:
            protocol = new NetworkProtocolTCP(receiveBuffer, transmitBuffer, specialBuffer);
            break;
        case "UDP"_sh:
            protocol = new NetworkProtocolUDP(receiveBuffer, transmitBuffer, specialBuffer);
            break;
        case "TEST"_sh:
            protocol = new NetworkProtocolTest(receiveBuffer, transmitBuffer, specialBuffer);
            break;
        case "TELNET"_sh:
            protocol = new NetworkProtocolTELNET(receiveBuffer, transmitBuffer, specialBuffer);
            break;
        case "TNFS"_sh:
            protocol = new NetworkProtocolTNFS(receiveBuffer, transmitBuffer, specialBuffer);
            break;
        case "FTP"_sh:
            protocol = new NetworkProtocolFTP(receiveBuffer, transmitBuffer, specialBuffer);
            break;
        case "HTTP"_sh:
        case "HTTPS"_sh:
            protocol = new NetworkProtocolHTTP(receiveBuffer, transmitBuffer, specialBuffer);
            break;
        case "SSH"_sh:
            protocol = new NetworkProtocolSSH(receiveBuffer, transmitBuffer, specialBuffer);
            break;
        case "SMB"_sh:
            protocol = new NetworkProtocolSMB(receiveBuffer, transmitBuffer, specialBuffer);
            break;
        default:
            Debug_printf("Invalid protocol: %s\n", scheme.c_str());
            break;
    }

    if (protocol && !login->empty()) {
        protocol->login = login;
        protocol->password = password;
    }

    return protocol;
}