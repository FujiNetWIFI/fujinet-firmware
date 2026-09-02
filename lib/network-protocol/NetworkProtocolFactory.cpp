#include "NetworkProtocolFactory.h"

#include "CLIPBOARD.h"
#include "CPM.h"
#include "GCAL.h"
#include "GDRIVE.h"
#include "GMAIL.h"
#include "ICAL.h"
#include "IMAPS.h"
#include "ONEDRIVE.h"
#include "TCP.h"
#include "UDP.h"
#include "Test.h"
#include "Telnet.h"
#include "TNFS.h"
#include "FTP.h"
#include "HTTP.h"
#include "WS.h"
#include "WSS.h"
#include "SSH.h"
#include "SFTP.h"
#include "SSHKeygen.h"
#include "SSHCopyId.h"
#include "SMB.h"
#include "NFS.h"
#include "S3.h"
#include "SD.h"
#include "string_utils.h"
#include "debug.h"

#include <algorithm>

std::unique_ptr<NetworkProtocol> NetworkProtocolFactory::createProtocol(std::string scheme, std::string *receiveBuffer, std::string *transmitBuffer, std::string *specialBuffer, std::string *login, std::string *password)
{
    std::unique_ptr<NetworkProtocol> protocol = nullptr;

    std::transform(scheme.begin(), scheme.end(), scheme.begin(), ::toupper);

    switch (hash_djb2a(scheme))
    {
    case "CLIPBOARD"_sh:
        protocol = std::make_unique<NetworkProtocolClipboard>(receiveBuffer, transmitBuffer, specialBuffer);
        break;
    case "CPM"_sh:
        protocol = std::make_unique<NetworkProtocolCPM>(receiveBuffer, transmitBuffer, specialBuffer);
        break;
    case "GCAL"_sh:
        protocol = std::make_unique<NetworkProtocolGCAL>(receiveBuffer, transmitBuffer, specialBuffer);
        break;
    case "GDRIVE"_sh:
        protocol = std::make_unique<NetworkProtocolGDRIVE>(receiveBuffer, transmitBuffer, specialBuffer);
        break;
    case "GMAIL"_sh:
        protocol = std::make_unique<NetworkProtocolGMAIL>(receiveBuffer, transmitBuffer, specialBuffer);
        break;
    // ICAL/WEBCAL fetch over https, ICALH over http; the class reads the scheme.
    case "ICAL"_sh:
    case "WEBCAL"_sh:
    case "ICALH"_sh:
        protocol = std::make_unique<NetworkProtocolICAL>(receiveBuffer, transmitBuffer, specialBuffer);
        break;
    case "IMAPS"_sh:
        protocol = std::make_unique<NetworkProtocolIMAPS>(receiveBuffer, transmitBuffer, specialBuffer);
        break;
    case "ONEDRIVE"_sh:
        protocol = std::make_unique<NetworkProtocolONEDRIVE>(receiveBuffer, transmitBuffer, specialBuffer);
        break;
    case "TCP"_sh:
        protocol = std::make_unique<NetworkProtocolTCP>(receiveBuffer, transmitBuffer, specialBuffer);
        break;
    case "UDP"_sh:
        protocol = std::make_unique<NetworkProtocolUDP>(receiveBuffer, transmitBuffer, specialBuffer);
        break;
    case "TEST"_sh:
        protocol = std::make_unique<NetworkProtocolTest>(receiveBuffer, transmitBuffer, specialBuffer);
        break;
    case "TELNET"_sh:
        protocol = std::make_unique<NetworkProtocolTELNET>(receiveBuffer, transmitBuffer, specialBuffer);
        break;
    case "TNFS"_sh:
        protocol = std::make_unique<NetworkProtocolTNFS>(receiveBuffer, transmitBuffer, specialBuffer);
        break;
    case "FTP"_sh:
        protocol = std::make_unique<NetworkProtocolFTP>(receiveBuffer, transmitBuffer, specialBuffer);
        break;
    case "HTTP"_sh:
    case "HTTPS"_sh:
        protocol = std::make_unique<NetworkProtocolHTTP>(receiveBuffer, transmitBuffer, specialBuffer);
        break;
    case "WS"_sh:
        protocol = std::make_unique<NetworkProtocolWS>(receiveBuffer, transmitBuffer, specialBuffer);
        break;
    case "WSS"_sh:
        protocol = std::make_unique<NetworkProtocolWSS>(receiveBuffer, transmitBuffer, specialBuffer);
        break;
    case "SSH"_sh:
        protocol = std::make_unique<NetworkProtocolSSH>(receiveBuffer, transmitBuffer, specialBuffer);
        break;
    case "SFTP"_sh:
        protocol = std::make_unique<NetworkProtocolSFTP>(receiveBuffer, transmitBuffer, specialBuffer);
        break;
    case "SSH.KEYGEN"_sh:
        protocol = std::make_unique<NetworkProtocolSSHKeygen>(receiveBuffer, transmitBuffer, specialBuffer);
        break;
    case "SSH.COPYID"_sh:
        protocol = std::make_unique<NetworkProtocolSSHCopyId>(receiveBuffer, transmitBuffer, specialBuffer);
        break;
    case "SMB"_sh:
        protocol = std::make_unique<NetworkProtocolSMB>(receiveBuffer, transmitBuffer, specialBuffer);
        break;
    case "NFS"_sh:
        protocol = std::make_unique<NetworkProtocolNFS>(receiveBuffer, transmitBuffer, specialBuffer);
        break;
    case "S3"_sh:
        protocol = std::make_unique<NetworkProtocolS3>(receiveBuffer, transmitBuffer, specialBuffer);
        break;
    case "SD"_sh:
        protocol = std::make_unique<NetworkProtocolSD>(receiveBuffer, transmitBuffer, specialBuffer);
        break;
    default:
        Debug_printf("Invalid protocol: %s\n", scheme.c_str());
        break;
    }

    if (protocol && login && !login->empty())
    {
        protocol->login = login;
        protocol->password = password;
    }

    return protocol;
}

