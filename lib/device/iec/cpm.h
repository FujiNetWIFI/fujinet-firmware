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

    /**
     * @brief Process command fanned out from bus
     * @param _commanddata the passed in commanddata
     * @return new device state
     */
    device_state_t process(IECData *_commanddata);

    protected:

    private:

    TaskHandle_t cpmTaskHandle = NULL;    

    virtual void poll_interrupt(unsigned char c);
    
    void iec_open();
    void iec_close();
    void iec_reopen();
    void iec_reopen_talk();
    void iec_reopen_listen();
};

#endif /* IECCPM_H */