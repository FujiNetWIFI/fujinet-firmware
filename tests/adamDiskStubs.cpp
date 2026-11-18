// Link-time stand-ins for adam_disk_tests, built against the AdamNet systemBus.
// Transactions are recorded in `bus`, as romMountStubs.cpp records them.

#include "adamnet.h"
#include "BoIPChannel.h"
#include "romMountStubs.h"
#include "utils.h"

systemBus SYSTEM_BUS;
BusLog bus;

BoIPChannel::BoIPChannel() : _ip(0), _port(0), _listening(false), _local_echo(false),
    _non_blocking(false), _no_delay(false), _fd(-1), _listen_fd(-1), _state(BoIPStopped) {}
BoIPChannel::~BoIPChannel() = default;
void BoIPChannel::updateFIFO() {}
size_t BoIPChannel::dataOut(const void *, size_t) { return 0; }
void BoIPChannel::end() {}
void BoIPChannel::flushOutput() {}

// fsFlash's directory listing; the tests never list a directory.
bool util_wildcard_match(const char *, const char *) { return false; }

void virtualDevice::reset() {}
void virtualDevice::adamnet_control_ready() {}
void virtualDevice::adamnet_control_receive() {}
void virtualDevice::adamnet_idle() {}

void systemBus::transaction_accept(transState_t) {}
void systemBus::transaction_success() { bus.outcome = Outcome::Success; }
void systemBus::transaction_error() { bus.outcome = Outcome::Error; }

size_result_t systemBus::transaction_get(void *data, size_t len)
{
    if (bus.incoming.size() < len)
        RETURN_SIZE_ERROR();
    memcpy(data, bus.incoming.data(), len);
    bus.incoming.erase(bus.incoming.begin(), bus.incoming.begin() + len);
    RETURN_SIZE(len);
}

void systemBus::transaction_send(const void *data, size_t len, bool is_error)
{
    bus.outcome = is_error ? Outcome::DataWithError : Outcome::Data;
    bus.answer.assign((const uint8_t *)data, (const uint8_t *)data + len);
}
