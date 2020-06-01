/* Modified version of ESP-Arduino fnTcpServer.cpp/h */

#include <lwip/sockets.h>
#include <lwip/netdb.h>
#include <errno.h>

#include "../../include/debug.h"

#include "fnDNS.h"
#include "fnTcpServer.h"

int fnTcpServer::setTimeout(uint32_t seconds)
{
    struct timeval tv;
    tv.tv_sec = seconds;
    tv.tv_usec = 0;
    if (setsockopt(_sockfd, SOL_SOCKET, SO_RCVTIMEO, (char *)&tv, sizeof(struct timeval)) < 0)
        return -1;
    return setsockopt(_sockfd, SOL_SOCKET, SO_SNDTIMEO, (char *)&tv, sizeof(struct timeval));
}

fnTcpClient fnTcpServer::available()
{
    if (!_listening)
        return fnTcpClient();
    int client_sock;
    if (_accepted_sockfd >= 0)
    {
        client_sock = _accepted_sockfd;
        _accepted_sockfd = -1;
    }
    else
    {
        struct sockaddr_in _client;
        int cs = sizeof(struct sockaddr_in);
        client_sock = lwip_accept_r(_sockfd, (struct sockaddr *)&_client, (socklen_t *)&cs);
    }
    if (client_sock >= 0)
    {
        int val = 1;
        if (setsockopt(client_sock, SOL_SOCKET, SO_KEEPALIVE, (char *)&val, sizeof(int)) == ESP_OK)
        {
            val = _noDelay;
            if (setsockopt(client_sock, IPPROTO_TCP, TCP_NODELAY, (char *)&val, sizeof(int)) == ESP_OK)
                return fnTcpClient(client_sock);
        }
    }
    return fnTcpClient();
}

void fnTcpServer::begin(uint16_t port)
{
    if (_listening)
        return;
    if (port)
    {
        _port = port;
    }
    struct sockaddr_in server;
    _sockfd = socket(AF_INET, SOCK_STREAM, 0);
    if (_sockfd < 0)
        return;
    server.sin_family = AF_INET;
    server.sin_addr.s_addr = INADDR_ANY;
    server.sin_port = htons(_port);
    if (bind(_sockfd, (struct sockaddr *)&server, sizeof(server)) < 0)
        return;
    if (listen(_sockfd, _max_clients) < 0)
        return;
    fcntl(_sockfd, F_SETFL, O_NONBLOCK);
    _listening = true;
    _noDelay = false;
    _accepted_sockfd = -1;
}

void fnTcpServer::setNoDelay(bool nodelay)
{
    _noDelay = nodelay;
}

bool fnTcpServer::getNoDelay()
{
    return _noDelay;
}

bool fnTcpServer::hasClient()
{
    if (_accepted_sockfd >= 0)
    {
        return true;
    }
    struct sockaddr_in _client;
    int cs = sizeof(struct sockaddr_in);
    _accepted_sockfd = lwip_accept_r(_sockfd, (struct sockaddr *)&_client, (socklen_t *)&cs);
    if (_accepted_sockfd >= 0)
    {
        return true;
    }
    return false;
}

void fnTcpServer::end()
{
    lwip_close_r(_sockfd);
    _sockfd = -1;
    _listening = false;
}

void fnTcpServer::stop()
{
    end();
}
