#ifndef TNFS_HARNESS_SERVER_H
#define TNFS_HARNESS_SERVER_H

// The scripted peer behind the stand-in fnTcpClient and fnUDP.

#include <algorithm>
#include <cstdint>
#include <deque>
#include <functional>
#include <vector>

#include "fnSystem.h"

class HarnessServer
{
public:
    using Bytes = std::vector<uint8_t>;

    // Called with each request. Queue what the client will read with deliver().
    std::function<void(const Bytes &request)> on_request;

    // Bytes that become readable `after_ms` from now. A delivery is one datagram over UDP;
    // over TCP, a read returns every byte that has arrived, so it can span deliveries.
    void deliver(const Bytes &bytes, uint32_t after_ms = 0)
    { _arrivals.push_back({bytes, fnSystem.now_us + uint64_t(after_ms) * 1000}); }

    size_t datagram_ready() const { return due(0) ? _arrivals.front().bytes.size() : 0; }

    size_t read_datagram(uint8_t *buf, size_t size)
    {
        size_t n = std::min(size, datagram_ready());
        if (datagram_ready() > 0)
        {
            std::copy_n(_arrivals.front().bytes.begin(), n, buf);
            _arrivals.pop_front();
        }
        return n;
    }

    size_t stream_ready() const
    {
        size_t n = 0;
        for (size_t i = 0; due(i); i++)
            n += _arrivals[i].bytes.size();
        return n;
    }

    size_t read_stream(uint8_t *buf, size_t size)
    {
        size_t n = 0;
        while (n < size && due(0))
        {
            Bytes &front = _arrivals.front().bytes;
            size_t take = std::min(size - n, front.size());
            std::copy_n(front.begin(), take, buf + n);
            front.erase(front.begin(), front.begin() + take);
            n += take;
            if (front.empty())
                _arrivals.pop_front();
        }
        return n;
    }

    bool drained() const { return _arrivals.empty(); }
    void discard() { _arrivals.clear(); }

    bool tcp_accepts = true; // connect() succeeds
    bool tcp_closing = false; // the peer closes once everything queued has been read
    int tcp_connects = 0;
    int udp_requests = 0;

private:
    bool due(size_t i) const
    { return i < _arrivals.size() && _arrivals[i].at_us <= fnSystem.now_us; }

    struct Arrival
    {
        Bytes bytes;
        uint64_t at_us;
    };
    std::deque<Arrival> _arrivals;
};

extern HarnessServer harness_server;

#endif
