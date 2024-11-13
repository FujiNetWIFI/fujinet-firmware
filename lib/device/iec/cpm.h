#ifndef IECCPM_H
#define IECCPM_H

#include "../../bus/bus.h"

#define FOLDERCHAR '/'

// Silly typedefs that runcpm uses
typedef unsigned char   uint8;
typedef unsigned short  uint16;
typedef unsigned int    uint32;

class iecCpm : public virtualDevice
{
    public:

    /**
     * @brief CTOR
     */
    iecCpm();

    /**
     * @brief DTOR
     */
    ~iecCpm();

#if 0
    /**
     * @brief Process command fanned out from bus
     * @return new device state
     */
    device_state_t process() override;
#endif

    protected:
    virtual device_state_t openChannel(/*int chan, IECPayload &payload*/) override;
    virtual device_state_t closeChannel(/*int chan*/) override;
    virtual device_state_t readChannel(/*int chan*/) override;
    virtual device_state_t writeChannel(/*int chan, IECPayload &payload*/) override;

    private:

    TaskHandle_t cpmTaskHandle = NULL;    

    virtual void poll_interrupt(unsigned char c) override;
    
    void iec_open();
    void iec_close();
    void iec_reopen();
    void iec_reopen_talk();
    void iec_reopen_listen();
};

#endif /* IECCPM_H */
