#ifndef NETWORK_PROTOCOL_FACTORY_H
#define NETWORK_PROTOCOL_FACTORY_H

#include "Protocol.h"
#include "CPM.h"
#include "FTP.h"
#include "GDRIVE.h"
#include "GMAIL.h"
#include "IMAPS.h"
#include "ONEDRIVE.h"
#include "HTTP.h"
#include "WS.h"
#include "WSS.h"
#include "SSH.h"
#include "SFTP.h"
#include "SMB.h"
#include "NFS.h"
#include "S3.h"
#include "TCP.h"
#include "Telnet.h"
#include "Test.h"
#include "TNFS.h"
#include "UDP.h"

#include <string>

class NetworkProtocolFactory
{
public:
    static std::unique_ptr<NetworkProtocol> createProtocol(std::string scheme, std::string *receiveBuffer, std::string *transmitBuffer, std::string *specialBuffer, std::string *login, std::string *password);
};

#endif // NETWORK_PROTOCOL_FACTORY_H
