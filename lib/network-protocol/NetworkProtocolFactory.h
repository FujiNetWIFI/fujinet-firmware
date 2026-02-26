#ifndef NETWORK_PROTOCOL_FACTORY_H
#define NETWORK_PROTOCOL_FACTORY_H

#include <algorithm>
#include <cctype>
#include <functional>
#include <memory>
#include <string>
#include <unordered_map>

#include "network_data.h"

#include "Protocol.h"
#include "FTP.h"
#include "HTTP.h"
#include "SSH.h"
#include "SMB.h"
#include "NFS.h"
#include "TCP.h"
#include "Telnet.h"
#include "Test.h"
#include "TNFS.h"
#include "UDP.h"

class NetworkProtocolFactory
{
public:
	static std::unique_ptr<NetworkProtocol> createProtocol(const std::string &scheme, NetworkData &data)
	{
		static const std::unordered_map<std::string, std::function<std::unique_ptr<NetworkProtocol>(NetworkData &)>> constructors = {
			{"FTP",    [](NetworkData &d) -> std::unique_ptr<NetworkProtocol> { return std::make_unique<NetworkProtocolFTP>(&d.receiveBuffer, &d.transmitBuffer, &d.specialBuffer); }},
			{"HTTP",   [](NetworkData &d) -> std::unique_ptr<NetworkProtocol> { return std::make_unique<NetworkProtocolHTTP>(&d.receiveBuffer, &d.transmitBuffer, &d.specialBuffer); }},
			{"HTTPS",  [](NetworkData &d) -> std::unique_ptr<NetworkProtocol> { return std::make_unique<NetworkProtocolHTTP>(&d.receiveBuffer, &d.transmitBuffer, &d.specialBuffer); }},
			{"SSH",    [](NetworkData &d) -> std::unique_ptr<NetworkProtocol> { return std::make_unique<NetworkProtocolSSH>(&d.receiveBuffer, &d.transmitBuffer, &d.specialBuffer); }},
			{"SMB",    [](NetworkData &d) -> std::unique_ptr<NetworkProtocol> { return std::make_unique<NetworkProtocolSMB>(&d.receiveBuffer, &d.transmitBuffer, &d.specialBuffer); }},
			{"NFS",    [](NetworkData &d) -> std::unique_ptr<NetworkProtocol> { return std::make_unique<NetworkProtocolNFS>(&d.receiveBuffer, &d.transmitBuffer, &d.specialBuffer); }},
			{"TCP",    [](NetworkData &d) -> std::unique_ptr<NetworkProtocol> { return std::make_unique<NetworkProtocolTCP>(&d.receiveBuffer, &d.transmitBuffer, &d.specialBuffer); }},
			{"TELNET", [](NetworkData &d) -> std::unique_ptr<NetworkProtocol> { return std::make_unique<NetworkProtocolTELNET>(&d.receiveBuffer, &d.transmitBuffer, &d.specialBuffer); }},
			{"TEST",   [](NetworkData &d) -> std::unique_ptr<NetworkProtocol> { return std::make_unique<NetworkProtocolTest>(&d.receiveBuffer, &d.transmitBuffer, &d.specialBuffer); }},
			{"TNFS",   [](NetworkData &d) -> std::unique_ptr<NetworkProtocol> { return std::make_unique<NetworkProtocolTNFS>(&d.receiveBuffer, &d.transmitBuffer, &d.specialBuffer); }},
			{"UDP",    [](NetworkData &d) -> std::unique_ptr<NetworkProtocol> { return std::make_unique<NetworkProtocolUDP>(&d.receiveBuffer, &d.transmitBuffer, &d.specialBuffer); }},
		};

		std::string scheme_upper = scheme;
		std::transform(scheme_upper.begin(), scheme_upper.end(), scheme_upper.begin(), [](unsigned char c){ return std::toupper(c); });

		auto it = constructors.find(scheme_upper);
		if (it != constructors.end())
		{
			auto p = it->second(data);
			p->name = scheme_upper;
			return p;
		}
		return nullptr;
	}
};

#endif // NETWORK_PROTOCOL_FACTORY_H