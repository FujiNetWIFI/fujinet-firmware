// Link-time stand-ins for iwm_disk_tests, built against the IWM systemBus.
// Transactions are recorded in `bus`, as romMountStubs.cpp records them, and
// the SmartPort error code of the last error in `last_sp_error`.

#include "iwm/iwmFuji.h"
#include "romMountStubs.h"
#include "utils.h"

systemBus SYSTEM_BUS;
BusLog bus;
spError_t last_sp_error = SP_ERR::NOERROR;

void virtualDevice::iwm_status(const iwm_decoded_cmd_t &) {}
void virtualDevice::iwm_readblock(const iwm_decoded_cmd_t &) {}
void virtualDevice::iwm_writeblock(const iwm_decoded_cmd_t &) {}
void virtualDevice::iwm_format(const iwm_decoded_cmd_t &) {}
void virtualDevice::iwm_ctrl(const iwm_decoded_cmd_t &) {}
void virtualDevice::iwm_open(const iwm_decoded_cmd_t &) {}
void virtualDevice::iwm_close(const iwm_decoded_cmd_t &) {}
void virtualDevice::iwm_read(const iwm_decoded_cmd_t &) {}
void virtualDevice::iwm_write(const iwm_decoded_cmd_t &) {}
IWMBusIDMap::busDeviceID_t virtualDevice::id() { return 1; }

// iwmDisk asks the Fuji device to forget an ejected disk; the tests never eject.
void iwmFuji::handle_ctl_eject(uint8_t) {}
fnFile *fujiHost::fnfile_open(const char *, char *, int, const char *) { return nullptr; }

// fsFlash's directory listing; the tests never list a directory.
bool util_wildcard_match(const char *, const char *) { return false; }

void systemBus::setFuji(fujiDevice *) {}
void systemBus::addDevice(virtualDevice *, fujiDeviceID_t, bool) {}
void systemBus::assignFujiIDToDevice(virtualDevice *, fujiDeviceID_t) {}
void systemBus::transaction_accept(transState_t) {}
void systemBus::transaction_success() { bus.outcome = Outcome::Success; }

void systemBus::transaction_error(spError_t err)
{
    bus.outcome = Outcome::Error;
    last_sp_error = err;
}

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
