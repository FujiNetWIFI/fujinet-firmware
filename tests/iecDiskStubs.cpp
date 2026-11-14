// Link-time stand-ins for iec_disk_tests: the global objects the real IEC bus
// reaches for in setup() and service(), which the tests never call, and the
// BoIP socket it holds.

#include "bus.h"
#include "fnConfig.h"
#include "fnDummyWiFi.h"
#include "fnSystem.h"
#include "led.h"
#include "BoIPChannel.h"

systemBus SYSTEM_BUS;

fnConfig Config;
fnConfig::fnConfig() {}

DummyWiFiManager fnWiFi;
DummyWiFiManager::~DummyWiFiManager() {}
bool DummyWiFiManager::connected() { return false; }

LedManager fnLedManager;
LedManager::LedManager() {}
void LedManager::set(eLed, bool) {}

SystemManager fnSystem;
SystemManager::SystemManager() {}
void SystemManager::delay(uint32_t) {}
uint64_t SystemManager::millis() { return 0; }
const char *SystemManager::get_fujinet_version(bool) { return ""; }
void SystemManager::reboot(uint32_t, bool) {}

BoIPChannel::BoIPChannel() : _ip(0), _port(0), _listening(false), _local_echo(false),
    _non_blocking(false), _no_delay(false), _fd(-1), _listen_fd(-1), _state(BoIPStopped) {}
BoIPChannel::~BoIPChannel() = default;
void BoIPChannel::updateFIFO() {}
size_t BoIPChannel::dataOut(const void *, size_t) { return 0; }
void BoIPChannel::end() {}
void BoIPChannel::flushOutput() {}
void BoIPChannel::begin(const BoIPConfig &) {}
