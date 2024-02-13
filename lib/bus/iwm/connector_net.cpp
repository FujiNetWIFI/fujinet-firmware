#ifdef BUILD_APPLE
#ifdef SP_OVER_SLIP

#include "connector_net.h"
#include "TCPConnection.h"

#include <iostream>
#include <sstream>

#include "fnConfig.h"
#include "fnDNS.h"

#ifdef WIN32

#include <winsock2.h>
#include <ws2tcpip.h>
#pragma comment(lib, "ws2_32.lib")
#define CLOSE_SOCKET closesocket
#define SHUTDOWN_SOCKET(s) shutdown(s, SD_SEND)
#define SOCKET_ERROR_CODE WSAGetLastError()

#else // !WIN32

#include <arpa/inet.h>
#include <errno.h>
#include <netinet/in.h>
#include <sys/socket.h>
#include <unistd.h>
#define CLOSE_SOCKET close
#define SHUTDOWN_SOCKET(s) shutdown(s, SHUT_WR)
#define SOCKET_ERROR_CODE errno
#define INVALID_SOCKET -1
#define SOCKET_ERROR -1

#endif

void connector_net::close_connection(bool report_error)
{
	if (sock == 0)
		return;

	if (SHUTDOWN_SOCKET(sock) == SOCKET_ERROR && report_error)
	{
		std::cerr << "Error shutting down socket, error code: " << SOCKET_ERROR_CODE << std::endl;
	}

	if (CLOSE_SOCKET(sock) == SOCKET_ERROR && report_error)
	{
		std::cerr << "Error closing socket, error code: " << SOCKET_ERROR_CODE << std::endl;
	}

#ifdef _WIN32
	WSACleanup();
#endif
	sock = 0;
}

std::shared_ptr<Connection> connector_net::create_connection()
{
	if (host_ip == IPADDR_NONE)
	{
		host_ip = get_ip4_addr_by_name(Config.get_boip_host().c_str());
		if (host_ip == IPADDR_NONE)
		{
			std::ostringstream msg;
			msg << "The host value " << Config.get_boip_host() << " could not be converted to an IP address";
			throw std::runtime_error(msg.str());
		}
	}
	if (host_port == 0) {
		host_port = Config.get_boip_port();
	}


#ifdef _WIN32
	WSADATA wsa_data;
	if (WSAStartup(MAKEWORD(2, 2), &wsa_data) != 0)
	{
		std::ostringstream msg;
		msg << "WSAStartup failed with code " << WSAGetLastError() << std::endl;
		throw std::runtime_error(msg.str());
	}
#endif
	int try_sock;
	if ((try_sock = socket(AF_INET, SOCK_STREAM, 0)) == INVALID_SOCKET)
	{
		std::ostringstream msg;
		msg << "A socket could not be created.";
		throw std::runtime_error(msg.str());
	}

	sockaddr_in server_addr{};
	server_addr.sin_family = AF_INET;
	server_addr.sin_port = htons(host_port);
	server_addr.sin_addr.s_addr = host_ip;

#ifdef _WIN32
	u_long mode = 1; // 1 to enable non-blocking socket
	ioctlsocket(try_sock, FIONBIO, &mode);
#endif

	bool did_connect = false;
	if (connect(try_sock, reinterpret_cast<sockaddr *>(&server_addr), sizeof(server_addr)) < 0)
	{
#ifdef _WIN32
		if (WSAGetLastError() == WSAEWOULDBLOCK)
		{
			// The connection attempt is in progress.
			fd_set writefds;
			FD_ZERO(&writefds);
			FD_SET(try_sock, &writefds);
			timeval tv;
			tv.tv_sec = 0;
			tv.tv_usec = 50000; // Wait up to 50 milliseconds for the connection to complete.
			if (select(0, NULL, &writefds, NULL, &tv) == 1)
			{
				did_connect = true;
			}
		}
		mode = 0; // 0 to disable non-blocking socket
		ioctlsocket(sock, FIONBIO, &mode);
#endif
		if (!did_connect)
		{
			sock = try_sock;
			close_connection(false);
			return nullptr;
		}
	}
	sock = try_sock;

	std::shared_ptr<Connection> conn = std::make_shared<TCPConnection>(sock);
	conn->set_is_connected(true);
	conn->create_read_channel();
	return conn;
}

#endif /* SP_OVER_SLIP */
#endif /* BUILD_APPLE */