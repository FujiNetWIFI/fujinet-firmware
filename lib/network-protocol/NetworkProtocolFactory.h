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
#include "TCP.h"
#include "Telnet.h"
#include "Test.h"
#include "TNFS.h"
#include "UDP.h"

class NetworkProtocolFactory
{
public:
	static std::shared_ptr<NetworkProtocol> createProtocol(const std::string &scheme, NetworkData &data)
	{
		static const std::unordered_map<std::string, std::function<std::shared_ptr<NetworkProtocol>(NetworkData &)>> constructors = {
			{"FTP",    [](NetworkData &d) -> std::shared_ptr<NetworkProtocol> { return std::make_shared<NetworkProtocolFTP>(&d.receiveBuffer, &d.transmitBuffer, &d.specialBuffer); }},
			{"HTTP",   [](NetworkData &d) -> std::shared_ptr<NetworkProtocol> { return std::make_shared<NetworkProtocolHTTP>(&d.receiveBuffer, &d.transmitBuffer, &d.specialBuffer); }},
			{"HTTPS",  [](NetworkData &d) -> std::shared_ptr<NetworkProtocol> { return std::make_shared<NetworkProtocolHTTP>(&d.receiveBuffer, &d.transmitBuffer, &d.specialBuffer); }},
			{"SSH",    [](NetworkData &d) -> std::shared_ptr<NetworkProtocol> { return std::make_shared<NetworkProtocolSSH>(&d.receiveBuffer, &d.transmitBuffer, &d.specialBuffer); }},
			{"SMB",    [](NetworkData &d) -> std::shared_ptr<NetworkProtocol> { return std::make_shared<NetworkProtocolSMB>(&d.receiveBuffer, &d.transmitBuffer, &d.specialBuffer); }},
			{"TCP",    [](NetworkData &d) -> std::shared_ptr<NetworkProtocol> { return std::make_shared<NetworkProtocolTCP>(&d.receiveBuffer, &d.transmitBuffer, &d.specialBuffer); }},
			{"TELNET", [](NetworkData &d) -> std::shared_ptr<NetworkProtocol> { return std::make_shared<NetworkProtocolTELNET>(&d.receiveBuffer, &d.transmitBuffer, &d.specialBuffer); }},
			{"TEST",   [](NetworkData &d) -> std::shared_ptr<NetworkProtocol> { return std::make_shared<NetworkProtocolTest>(&d.receiveBuffer, &d.transmitBuffer, &d.specialBuffer); }},
			{"TNFS",   [](NetworkData &d) -> std::shared_ptr<NetworkProtocol> { return std::make_shared<NetworkProtocolTNFS>(&d.receiveBuffer, &d.transmitBuffer, &d.specialBuffer); }},
			{"UDP",    [](NetworkData &d) -> std::shared_ptr<NetworkProtocol> { return std::make_shared<NetworkProtocolUDP>(&d.receiveBuffer, &d.transmitBuffer, &d.specialBuffer); }},
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