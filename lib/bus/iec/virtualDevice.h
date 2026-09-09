#ifndef VIRTUALDEVICE_H
#define VIRTUALDEVICE_H

#ifdef BUILD_IEC

#include "IECDevice.h"
#include "FujiIECPacket.h"

#include <memory>

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

class virtualDevice : public IECDevice
{
protected:
    virtual void shutdown() {};

    virtual bool processCommand(const FujiIECPacket &packet) = 0;

    // FIXME - this should all be handled by the bus, not individual devices
    ByteBuffer _payload;
    std::unique_ptr<FujiIECPacket> _activePacket;
    device_state_t _state;
    void talk(uint8_t secondary) override;
    void listen(uint8_t secondary) override;
    void untalk() override;
    void unlisten() override;
    int8_t canWrite() override;
    int8_t canRead() override;
    void write(uint8_t data, bool eoi) override;
    uint8_t read() override;
    void task() override;
    void process_cmd();

#ifdef UNUSED
    // IEC channel methods
    bool open(uint8_t channel, const char *name) override;
    void close(uint8_t channel) override;
    uint8_t read(uint8_t channel, uint8_t *buffer, uint8_t bufferSize, bool *eoi) override;
    uint8_t write(uint8_t channel, uint8_t *buffer, uint8_t bufferSize, bool eoi) override;
#endif /* UNUSED */

 public:
    virtualDevice() = default;
    virtualDevice(uint8_t devnr) : IECDevice(devnr) {}
};

#endif /* BUILD_IEC */

#endif /* VIRTUALDEVICE_H */
