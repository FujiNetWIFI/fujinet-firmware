#ifndef IEC_H
#define IEC_H

#include "FujiIECPacket.h"
#include "IECDevice.h"
#include "IECBusHandler.h"
#include "string_utils.h"

#define FUJI_COMMAND_PACKET FujiIECPacket

#define BUS_DEVICEID_PRINTER 4
#define BUS_DEVICEID_DISK 8
#define BUS_DEVICEID_NETWORK 16

// FIXME - belongs in IECBusHandler
typedef enum
{
    DEVICE_ERROR = -1,
    DEVICE_IDLE = 0,      // Ready and waiting
    DEVICE_ACTIVE = 1,
    DEVICE_LISTEN = 2,    // A command is recieved and data is coming to us
    DEVICE_TALK = 3,      // A command is recieved and we must talk now
    DEVICE_PAUSED = 4,    // Execute device command
} device_state_t;

enum {
  OPCODE_NO_PAYLOAD  = 0x01,
  OPCODE_HAS_PAYLOAD = 0x02,
};

class iecDrive;
class iecFuji;

/**
 * @class systemBus
 * @brief the system bus that all virtualDevices attach to.
 */
class systemBus : public IECBusHandler, public SystemBusBase
{
public:
    systemBus();

    /**
     * @brief called in main.cpp to set up the bus.
     */
    void setup();

    /**
     * @brief Run one iteration of the bus service loop
     */
    void service();

    /**
     * @brief called from main shutdown to clean up the device.
     */
    void shutdown();

    /**
     * @brief Are we shutting down?
     * @return value of shuttingDown
     */
    bool getShuttingDown() { return shuttingDown; }

    // needed for fujiDevice compatibility
    void changeDeviceId(iecDrive *pDevice, int device_id);

    std::string nativeTextToUnicode(const std::string &native) override {
        return mstr::toUTF8(native);
    }
    virtual std::string unicodeTextToNative(const std::string &unicode) override {
        return mstr::toPETSCII2(unicode);
    }

    void transaction_accept(transState_t expectMoreData) override;
    void transaction_success() override;
    void transaction_error() override;
    success_is_true transaction_get(void *data, size_t len) override;
    using SystemBusBase::transaction_send;
    void transaction_send(const void *data, size_t len, bool is_error=false) override;

    // FIXME - bus should be creating the packet
    FujiIECPacket *_activePacket {};

    // FIXME - belongs in IECBusHandler, not here or in devices
    int8_t iecCanRead(device_state_t state) {
        if (state == DEVICE_TALK)
            return std::min((size_t) 2, _transaction_response.size());
        return 0;
    }
    uint8_t iecRead() {
        uint8_t val = 0;
        if (_transaction_response.size())
        {
            val = _transaction_response.front();
            _transaction_response.erase(_transaction_response.begin());
        }
        return val;
    }

    void rotateDevices(const std::vector<iecDrive *> &devices, int amount) {}
    fujiDeviceID_t fujiIDForDevice(iecDrive *device);

 private:
    /**
     * @brief is device shutting down?
     */
    bool shuttingDown = false;

    ByteBuffer _transaction_response;

};
/**
 * @brief Return
 */
extern systemBus SYSTEM_BUS;

#endif /* IEC_H */
