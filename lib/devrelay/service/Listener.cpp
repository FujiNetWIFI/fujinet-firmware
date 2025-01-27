#if defined(DEV_RELAY_SLIP) && defined(SLIP_PROTOCOL_NET)

#include <atomic>
#include <chrono>
#include <functional>
#include <iostream>
#include <thread>

#include "Listener.h"

#include "../commands/Init.h"
#include "../commands/Status.h"
#include "../slip/SLIP.h"

#include "Log.h"
#include "Requestor.h"
#include "TCPConnection.h"

#ifdef WIN32
	#include <winsock2.h>
	#include <ws2tcpip.h>
	#pragma comment(lib, "ws2_32.lib")
	#define CLOSE_SOCKET closesocket
	#define SOCKET_ERROR_CODE WSAGetLastError()
#else
	#include <arpa/inet.h>
	#include <errno.h>
	#include <netinet/in.h>
	#include <sys/socket.h>
	#include <sys/types.h>
	#include <unistd.h>
	#define CLOSE_SOCKET close
	#define SOCKET_ERROR_CODE errno
	#define INVALID_SOCKET -1
	#define SOCKET_ERROR -1
#endif

const std::regex Listener::ipPattern("^((25[0-5]|2[0-4][0-9]|[01]?[0-9][0-9]?)\\.){3}(25[0-5]|2[0-4][0-9]|[01]?[0-9][0-9]?)$");

Listener &GetCommandListener(void)
{
	static Listener listener;
	return listener;
}

Listener::Listener() : is_listening_(false) {}

void Listener::Initialize(std::string ip_address, const uint16_t port)
{
	ip_address_ = std::move(ip_address);
	port_ = port;
}

bool Listener::get_is_listening() const { return is_listening_; }

void Listener::insert_connection(uint8_t host_id, const ConnectionInfo &info) { connection_info_map_[host_id] = info; }

uint8_t Listener::get_total_device_count() { return static_cast<uint8_t>(connection_info_map_.size()); }

Listener::~Listener() { stop(); }

std::thread Listener::create_listener_thread() { return std::thread(&Listener::listener_function, this); }

void Listener::listener_function()
{
	LogFileOutput("Listener::listener_function - RUNNING\n");
	int server_fd, new_socket;
	struct sockaddr_in address;
#ifdef WIN32
	int address_length = sizeof(address);
#else
	socklen_t address_length = sizeof(address);
#endif

#ifdef WIN32
	WSADATA wsa_data;
	if (WSAStartup(MAKEWORD(2, 2), &wsa_data) != 0)
	{
		LogFileOutput("WSAStartup failed: %d\n", WSAGetLastError());
	}
#endif

	if ((server_fd = socket(AF_INET, SOCK_STREAM, 0)) == INVALID_SOCKET)
	{
		LogFileOutput("Listener::listener_function - Socket creation failed\n");
		return;
	}

	address.sin_family = AF_INET;
	address.sin_port = htons(port_);
	inet_pton(AF_INET, ip_address_.c_str(), &(address.sin_addr));

#ifdef WIN32
	char opt = 1;
#else
	int opt = 1;
#endif
	if (setsockopt(server_fd, SOL_SOCKET, SO_REUSEADDR, &opt, sizeof(opt)) == SOCKET_ERROR)
	{
		LogFileOutput("Listener::listener_function - setsockopt failed\n");
		return;
	}
#ifdef WIN32
	if (bind(server_fd, reinterpret_cast<SOCKADDR *>(&address), sizeof(address)) == SOCKET_ERROR)
#else
	if (bind(server_fd, (struct sockaddr *)&address, sizeof(address)) == SOCKET_ERROR)
#endif
	{
		LogFileOutput("Listener::listener_function - bind failed\n");
		return;
	}

	if (listen(server_fd, 3) < 0)
	{
		LogFileOutput("Listener::listener_function - listen failed\n");
		return;
	}

	while (is_listening_)
	{
		fd_set sock_set;
		FD_ZERO(&sock_set);
		FD_SET(server_fd, &sock_set);

		timeval timeout;
		timeout.tv_sec = 2;
		timeout.tv_usec = 0;

		const int activity = select(server_fd + 1, &sock_set, nullptr, nullptr, &timeout);

		if (activity == SOCKET_ERROR)
		{
			LogFileOutput("Listener::listener_function - select failed\n");
			is_listening_ = false;
			break;
		}

		if (activity == 0)
		{
			// timeout occurred, no client connection. Still need to check is_listening_
			continue;
		}

#ifdef WIN32
		if ((new_socket = accept(server_fd, reinterpret_cast<SOCKADDR *>(&address), &address_length)) == INVALID_SOCKET)
#else
		if ((new_socket = accept(server_fd, (struct sockaddr *)&address, &address_length)) == INVALID_SOCKET)
#endif
		{
			LogFileOutput("Listener::listener_function - accept failed\n");
			is_listening_ = false;
			break;
		}

		create_connection(new_socket);
	}

	LogFileOutput("Listener::listener_function - listener closing down\n");

	CLOSE_SOCKET(server_fd);
}

// Creates a Connection object, which is how SP device(s) will register itself with our listener.
void Listener::create_connection(unsigned int socket)
{
	// Create a connection, give it some time to settle, else exit without creating listener to connection
	const std::shared_ptr<Connection> conn = std::make_shared<TCPConnection>(socket);
	conn->create_read_channel();

	const auto start = std::chrono::steady_clock::now();
	// Give the connection a generous 10 seconds to work.
	constexpr auto timeout = std::chrono::seconds(10);

	while (!conn->is_connected())
	{
		auto now = std::chrono::steady_clock::now();
		if (std::chrono::duration_cast<std::chrono::seconds>(now - start) > timeout)
		{
			LogFileOutput("Listener::create_connection() - Failed to establish "
						  "connection, timed out.\n");
			return;
		}
	}

	// We need to send an INIT to device 01 for this connection, then 02, ...
	// until we get an error back This will determine the number of devices
	// attached.

	bool still_scanning = true;
	uint8_t device_id = 1;
	uint8_t host_id = 1;

	// send init requests to find all the devices on this connection, or we have too many devices.
	while (still_scanning && connection_info_map_.size() < 254)
	{
		LogFileOutput("SmartPortOverSlip listener sending request for device_id: %d\n", device_id);
		InitRequest request(Requestor::next_request_number(), 1, device_id);
		const auto response = Requestor::send_request(request, conn.get());
		const auto init_response = dynamic_cast<InitResponse *>(response.get());
		if (init_response == nullptr)
		{
			LogFileOutput("SmartPortOverSlip listener ERROR, no response data found\n");
			break;
		}
		still_scanning = init_response->get_status() == 0;

		// find the next available host_id this device can map to
		while (connection_info_map_.find(host_id) != connection_info_map_.end())
		{
			host_id++;
		}

		// create an info object for the connection... no names!
		ConnectionInfo info;
		info.host_id = host_id;
		info.device_id = device_id;
		info.connection = conn;
		LogFileOutput("SmartPortOverSlip listener creating connection entry for host_id: %d, device_id: %d\n", host_id, device_id);
		insert_connection(host_id, info);

		if (still_scanning)
			device_id++;
	}

}

void Listener::start()
{
	is_listening_ = true;
	listening_thread_ = std::thread(&Listener::listener_function, this);
}

void Listener::stop()
{
	LogFileOutput("Listener::stop()\n");
	if (is_listening_)
	{
		// Stop listener first, otherwise the PC might reboot too fast and be picked up
		is_listening_ = false;
		LogFileOutput("Listener::stop() ... joining listener until it stops\n");
		listening_thread_.join();

		LogFileOutput("Listener::stop() - closing %ld connections\n", connection_info_map_.size());
		for (auto &pair : connection_info_map_)
		{
			const auto &connection = pair.second.connection;
			connection->set_is_connected(false);
			connection->close_connection();
			connection->join();
		}
	}
	connection_info_map_.clear();

#ifdef WIN32
	WSACleanup();
#endif

	LogFileOutput("Listener::stop() ... finished\n");
}

// Returns the target's device id, and connection to it from the host_id supplied by caller.
// We store (for example) device_ids in applewin with values 1-5 for connection 1, 6-8 for connection 2, but each device thinks they are 1-5, and 1-3 (not 6-8).
// However the apple side sees 1-8, and so we have to convert 6, 7, 8 into the target's 1, 2, 3
std::pair<uint8_t, std::shared_ptr<Connection>> Listener::find_connection_with_device(const uint8_t host_device_id) const
{
	std::pair<uint8_t, std::shared_ptr<Connection>> result;

	auto it = connection_info_map_.find(host_device_id);
	if (it != connection_info_map_.end())
	{
		auto connection_info = it->second;
		result = std::make_pair(connection_info.device_id, connection_info.connection);
	}
	return result;
}

std::vector<std::pair<uint8_t, Connection *>> Listener::get_all_connections() const
{
	std::vector<std::pair<uint8_t, Connection *>> connections;
	for (const auto &kv : connection_info_map_)
	{
		connections.emplace_back(kv.first, kv.second.connection.get());
	}
	return connections;
}

std::pair<int, int> Listener::first_two_disk_devices(std::function<bool(const std::vector<uint8_t>&)> is_disk_device) const
{
	std::pair<int, int> disk_ids = {-1, -1};
	const auto connections = GetCommandListener().get_all_connections();
	for (const auto &id_and_connection : connections)
	{
		const uint8_t unit_number = id_and_connection.first;

		// DIB request to get information block. We need the device id the target understands here, not the unit_number from the ids maintained by host
		const StatusRequest request(Requestor::next_request_number(), 3, id_and_connection.first, 3, 0); // no network unit here

		std::unique_ptr<Response> response = Requestor::send_request(request, id_and_connection.second);

		// Cast the Response to a StatusResponse
		StatusResponse *statusResponse = dynamic_cast<StatusResponse *>(response.get());

		if (statusResponse)
		{
			const std::vector<uint8_t> &data = statusResponse->get_data();

			if (is_disk_device(data))
			{
				// We use the unique unit_number below, as eventually we'll look these up again in the Listener's map to find a connection.

				// If first disk device id is not set, set it
				if (disk_ids.first == -1)
				{
					disk_ids.first = unit_number;
				}
				// Else if second disk device id is not set, set it and break the loop
				else if (disk_ids.second == -1)
				{
					disk_ids.second = unit_number;
					break;
				}
			}
		}
	}

	return disk_ids;
}

void Listener::connection_closed(Connection *connection)
{
	for (auto it = connection_info_map_.begin(); it != connection_info_map_.end();)
	{
		if (it->second.connection.get() == connection)
		{
			LogFileOutput("Removing device with id: %d from listener\n", it->first);
			it = connection_info_map_.erase(it);
		}
		else
		{
			++it;
		}
	}
}


#endif
