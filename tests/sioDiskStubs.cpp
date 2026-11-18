// Link-time stand-ins for sio_disk_tests, built against the SIO systemBus.
// Transactions are recorded in `bus`, as romMountStubs.cpp records them.

#include "sio/sioFuji.h"
#include "fnSystem.h"
#include "romMountStubs.h"

systemBus SYSTEM_BUS;
BusLog bus;

NetSIO::NetSIO()
    : _ip(0), _port(0), _baud(0), _baud_peer(0), _fd(-1),
      _initialized(false), _command_asserted(false), _motor_asserted(false),
      _sync_request_num(-1), _sync_ack_byte(0), _sync_write_size(0),
      _errcount(0), _resume_time(0), _alive_time(0), _alive_request(0),
      _credit(0)
{
}
NetSIO::~NetSIO() {}
void NetSIO::updateFIFO() {}
size_t NetSIO::dataOut(const void *, size_t) { return 0; }
void NetSIO::end() {}
void NetSIO::flushOutput() {}
void NetSIO::setBaudrate(uint32_t) {}

void virtualDevice::sio_high_speed() {}

// What the SIO packet, the bus, and the XEX and ATX formats reach for, none of
// which the tests exercise.
SystemManager fnSystem;
SystemManager::SystemManager() {}
uint64_t SystemManager::micros() { return 0; }
void SystemManager::delay_microseconds(uint32_t) {}
void SystemManager::yield() {}
int SystemManager::load_firmware(const char *, uint8_t *) { return -1; }
uint8_t sio_checksum(uint8_t *, unsigned short) { return 0; }
void NetSIO::setWriteSize(int) {}
fnFile *fujiHost::fnfile_open(const char *, char *, int, const char *) { return nullptr; }

// sioDisk hands CAS and WAV images to the cassette, which the tests never mount.
void sioCassette::mount_cassette_file(fnFile *, size_t) {}
void sioCassette::stop_fsk_loader() {}
void sioCassette::umount_cassette_file() {}
void sioCassette::sio_disable_cassette() {}

void systemBus::toggleBaudrate() {}
void systemBus::_sio_ack() {}
void systemBus::_sio_error() {}
void systemBus::setFuji(fujiDevice *) {}
void systemBus::addDevice(virtualDevice *, fujiDeviceID_t) {}
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
